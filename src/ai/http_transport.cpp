#include "horusrf/ai/ollama_provider.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace horusrf::ai::ollama {
namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket invalid_socket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket invalid_socket = -1;
#endif

constexpr std::size_t maximum_response_size = 16U * 1024U * 1024U;

int socket_error() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool operation_in_progress(int error) {
#ifdef _WIN32
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS ||
           error == WSAEINVAL;
#else
    return error == EINPROGRESS || error == EWOULDBLOCK || error == EAGAIN;
#endif
}

void close_socket(NativeSocket socket) {
    if (socket == invalid_socket) return;
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

class Socket final {
public:
    explicit Socket(NativeSocket value = invalid_socket) : value_(value) {}
    ~Socket() { close_socket(value_); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : value_(std::exchange(other.value_, invalid_socket)) {}
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            close_socket(value_);
            value_ = std::exchange(other.value_, invalid_socket);
        }
        return *this;
    }
    [[nodiscard]] NativeSocket get() const { return value_; }
    [[nodiscard]] explicit operator bool() const { return value_ != invalid_socket; }

private:
    NativeSocket value_;
};

#ifdef _WIN32
class Winsock final {
public:
    Winsock() {
        WSADATA data{};
        ready_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~Winsock() {
        if (ready_) WSACleanup();
    }
    [[nodiscard]] bool ready() const { return ready_; }

private:
    bool ready_{};
};
#endif

struct ParsedUrl final {
    std::string host;
    std::string port;
    std::string path;
    std::string authority;
};

std::variant<ParsedUrl, TransportError> parse_url(std::string_view url) {
    constexpr std::string_view scheme = "http://";
    if (!url.starts_with(scheme)) {
        return TransportError{TransportErrorCode::InvalidEndpoint,
                              "only http:// Ollama endpoints are supported"};
    }
    url.remove_prefix(scheme.size());
    const std::size_t path_begin = url.find('/');
    const std::string_view authority = url.substr(0, path_begin);
    const std::string_view path = path_begin == std::string_view::npos
                                      ? std::string_view{"/"}
                                      : url.substr(path_begin);
    if (authority.empty() || authority.find('@') != std::string_view::npos ||
        path.find('#') != std::string_view::npos) {
        return TransportError{TransportErrorCode::InvalidEndpoint,
                              "the Ollama endpoint is not a valid HTTP URL"};
    }

    std::string_view host;
    std::string_view port = "80";
    if (authority.front() == '[') {
        const std::size_t closing = authority.find(']');
        if (closing == std::string_view::npos || closing == 1) {
            return TransportError{TransportErrorCode::InvalidEndpoint,
                                  "the Ollama endpoint has an invalid IPv6 host"};
        }
        host = authority.substr(1, closing - 1);
        if (closing + 1 < authority.size()) {
            if (authority[closing + 1] != ':' || closing + 2 == authority.size()) {
                return TransportError{TransportErrorCode::InvalidEndpoint,
                                      "the Ollama endpoint has an invalid port"};
            }
            port = authority.substr(closing + 2);
        }
    } else {
        const std::size_t colon = authority.rfind(':');
        if (colon != std::string_view::npos) {
            if (authority.find(':') != colon || colon == 0 || colon + 1 == authority.size()) {
                return TransportError{TransportErrorCode::InvalidEndpoint,
                                      "the Ollama endpoint has an invalid host or port"};
            }
            host = authority.substr(0, colon);
            port = authority.substr(colon + 1);
        } else {
            host = authority;
        }
    }
    unsigned port_number{};
    const auto port_result = std::from_chars(port.data(), port.data() + port.size(), port_number);
    if (port_result.ec != std::errc{} || port_result.ptr != port.data() + port.size() ||
        port_number == 0 || port_number > 65535) {
        return TransportError{TransportErrorCode::InvalidEndpoint,
                              "the Ollama endpoint has an invalid port"};
    }
    if (host.empty() || path.empty() || path.front() != '/') {
        return TransportError{TransportErrorCode::InvalidEndpoint,
                              "the Ollama endpoint is not a valid HTTP URL"};
    }
    return ParsedUrl{std::string{host}, std::string{port}, std::string{path},
                     std::string{authority}};
}

bool set_nonblocking(NativeSocket socket) {
#ifdef _WIN32
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

enum class WaitResult { Ready, Timeout, Cancelled, Failed };

WaitResult wait_for(NativeSocket socket, bool write,
                    std::chrono::steady_clock::time_point deadline,
                    CancellationToken cancellation) {
    using namespace std::chrono;
    for (;;) {
        if (cancellation.stop_requested()) return WaitResult::Cancelled;
        const auto now = steady_clock::now();
        if (now >= deadline) return WaitResult::Timeout;
        const auto remaining = duration_cast<microseconds>(deadline - now);
        const auto slice = std::min(remaining, duration_cast<microseconds>(milliseconds{50}));
        timeval timeout{static_cast<long>(slice.count() / 1'000'000),
                        static_cast<long>(slice.count() % 1'000'000)};
        fd_set descriptors;
        FD_ZERO(&descriptors);
        FD_SET(socket, &descriptors);
        const int count = select(static_cast<int>(socket) + 1,
                                 write ? nullptr : &descriptors,
                                 write ? &descriptors : nullptr, nullptr, &timeout);
        if (count > 0) return WaitResult::Ready;
        if (count == 0) continue;
#ifndef _WIN32
        if (errno == EINTR) continue;
#endif
        return WaitResult::Failed;
    }
}

TransportError wait_error(WaitResult result, std::string_view operation) {
    switch (result) {
    case WaitResult::Timeout:
        return {TransportErrorCode::Timeout,
                "the Ollama " + std::string{operation} + " timed out"};
    case WaitResult::Cancelled:
        return {TransportErrorCode::Cancelled, "the Ollama request was cancelled"};
    case WaitResult::Failed:
        return {TransportErrorCode::ConnectionFailure,
                "the Ollama " + std::string{operation} + " failed"};
    case WaitResult::Ready: break;
    }
    return {TransportErrorCode::ConnectionFailure, "the Ollama request failed"};
}

std::variant<Socket, TransportError> connect_to(
    const ParsedUrl& url, std::chrono::steady_clock::time_point deadline,
    CancellationToken cancellation) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* addresses{};
    const int lookup = getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &addresses);
    if (lookup != 0) {
        return TransportError{TransportErrorCode::ConnectionFailure,
                              "cannot resolve the Ollama endpoint"};
    }
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> owned{addresses, freeaddrinfo};

    for (const addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        if (cancellation.stop_requested()) {
            return TransportError{TransportErrorCode::Cancelled,
                                  "the Ollama request was cancelled"};
        }
        Socket socket{::socket(address->ai_family, address->ai_socktype,
                               address->ai_protocol)};
        if (!socket || !set_nonblocking(socket.get())) continue;
        if (::connect(socket.get(), address->ai_addr,
                      static_cast<int>(address->ai_addrlen)) == 0) {
            return socket;
        }
        if (!operation_in_progress(socket_error())) continue;
        const WaitResult waited = wait_for(socket.get(), true, deadline, cancellation);
        if (waited == WaitResult::Cancelled || waited == WaitResult::Timeout) {
            return wait_error(waited, "connection");
        }
        if (waited != WaitResult::Ready) continue;
        int connect_error{};
#ifdef _WIN32
        int length = sizeof(connect_error);
#else
        socklen_t length = sizeof(connect_error);
#endif
        if (getsockopt(socket.get(), SOL_SOCKET, SO_ERROR,
                       reinterpret_cast<char*>(&connect_error), &length) == 0 &&
            connect_error == 0) {
            return socket;
        }
    }
    return TransportError{TransportErrorCode::ConnectionFailure,
                          "cannot connect to the Ollama endpoint"};
}

std::optional<TransportError> send_all(
    NativeSocket socket, std::string_view request,
    std::chrono::steady_clock::time_point deadline, CancellationToken cancellation) {
    std::size_t sent{};
    while (sent < request.size()) {
        const WaitResult waited = wait_for(socket, true, deadline, cancellation);
        if (waited != WaitResult::Ready) return wait_error(waited, "request");
        const std::size_t remaining = request.size() - sent;
        const int chunk = static_cast<int>(std::min(
            remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
#ifdef _WIN32
        const int count = send(socket, request.data() + sent, chunk, 0);
#else
        const int count = static_cast<int>(
            send(socket, request.data() + sent, static_cast<std::size_t>(chunk), MSG_NOSIGNAL));
#endif
        if (count > 0) {
            sent += static_cast<std::size_t>(count);
            continue;
        }
        const int error = socket_error();
        if (operation_in_progress(error)) continue;
        return TransportError{TransportErrorCode::ConnectionFailure,
                              "the Ollama request could not be sent"};
    }
    return std::nullopt;
}

std::variant<std::string, TransportError> receive_all(
    NativeSocket socket, std::chrono::steady_clock::time_point deadline,
    CancellationToken cancellation) {
    std::string response;
    char buffer[8192];
    for (;;) {
        const WaitResult waited = wait_for(socket, false, deadline, cancellation);
        if (waited != WaitResult::Ready) return wait_error(waited, "response");
#ifdef _WIN32
        const int count = recv(socket, buffer, static_cast<int>(sizeof(buffer)), 0);
#else
        const int count = static_cast<int>(recv(socket, buffer, sizeof(buffer), 0));
#endif
        if (count == 0) return response;
        if (count < 0) {
            if (operation_in_progress(socket_error())) continue;
            return TransportError{TransportErrorCode::ConnectionFailure,
                                  "the Ollama response could not be read"};
        }
        response.append(buffer, static_cast<std::size_t>(count));
        if (response.size() > maximum_response_size) {
            return TransportError{TransportErrorCode::InvalidResponse,
                                  "the Ollama response exceeded 16 MiB"};
        }
    }
}

std::string lower_ascii(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return value;
}

std::variant<std::string, TransportError> decode_chunks(std::string_view body) {
    std::string decoded;
    std::size_t position{};
    for (;;) {
        const std::size_t line_end = body.find("\r\n", position);
        if (line_end == std::string_view::npos) {
            return TransportError{TransportErrorCode::InvalidResponse,
                                  "the Ollama response has an invalid chunk header"};
        }
        std::string_view size_text = body.substr(position, line_end - position);
        if (const std::size_t extension = size_text.find(';');
            extension != std::string_view::npos) size_text = size_text.substr(0, extension);
        std::size_t size{};
        const auto parsed = std::from_chars(size_text.data(),
                                            size_text.data() + size_text.size(), size, 16);
        if (size_text.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != size_text.data() + size_text.size()) {
            return TransportError{TransportErrorCode::InvalidResponse,
                                  "the Ollama response has an invalid chunk size"};
        }
        position = line_end + 2;
        if (size == 0) return decoded;
        if (size > body.size() - position || body.size() - position - size < 2 ||
            body.substr(position + size, 2) != "\r\n") {
            return TransportError{TransportErrorCode::InvalidResponse,
                                  "the Ollama response contains an incomplete chunk"};
        }
        decoded.append(body.substr(position, size));
        if (decoded.size() > maximum_response_size) {
            return TransportError{TransportErrorCode::InvalidResponse,
                                  "the Ollama response exceeded 16 MiB"};
        }
        position += size + 2;
    }
}

std::variant<HttpResponse, TransportError> parse_http_response(std::string_view response) {
    const std::size_t header_end = response.find("\r\n\r\n");
    if (header_end == std::string_view::npos) {
        return TransportError{TransportErrorCode::InvalidResponse,
                              "the Ollama endpoint returned an invalid HTTP response"};
    }
    const std::size_t status_end = response.find("\r\n");
    if (status_end == std::string_view::npos || !response.starts_with("HTTP/1.")) {
        return TransportError{TransportErrorCode::InvalidResponse,
                              "the Ollama endpoint returned an invalid HTTP status"};
    }
    const std::size_t first_space = response.find(' ');
    if (first_space == std::string_view::npos || first_space + 4 > status_end) {
        return TransportError{TransportErrorCode::InvalidResponse,
                              "the Ollama endpoint returned an invalid HTTP status"};
    }
    int status{};
    const std::string_view status_text = response.substr(first_space + 1, 3);
    const auto status_result = std::from_chars(status_text.data(),
                                               status_text.data() + status_text.size(), status);
    if (status_result.ec != std::errc{} || status < 100 || status > 599) {
        return TransportError{TransportErrorCode::InvalidResponse,
                              "the Ollama endpoint returned an invalid HTTP status"};
    }

    bool chunked{};
    std::optional<std::size_t> content_length;
    std::size_t line_begin = status_end + 2;
    while (line_begin < header_end) {
        const std::size_t line_end = response.find("\r\n", line_begin);
        if (line_end == std::string_view::npos || line_end > header_end) break;
        const std::string_view line = response.substr(line_begin, line_end - line_begin);
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            return TransportError{TransportErrorCode::InvalidResponse,
                                  "the Ollama endpoint returned an invalid HTTP header"};
        }
        const std::string name = lower_ascii(std::string{line.substr(0, colon)});
        std::string_view value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
            value.remove_prefix(1);
        }
        if (name == "transfer-encoding" && lower_ascii(std::string{value}) == "chunked") {
            chunked = true;
        } else if (name == "content-length") {
            std::size_t length{};
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), length);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
                return TransportError{TransportErrorCode::InvalidResponse,
                                      "the Ollama endpoint returned an invalid content length"};
            }
            content_length = length;
        }
        line_begin = line_end + 2;
    }
    const std::string_view body = response.substr(header_end + 4);
    if (chunked) {
        auto decoded = decode_chunks(body);
        if (const auto* error = std::get_if<TransportError>(&decoded)) return *error;
        return HttpResponse{status, std::get<std::string>(std::move(decoded))};
    }
    if (content_length && *content_length != body.size()) {
        return TransportError{TransportErrorCode::InvalidResponse,
                              "the Ollama endpoint returned an incomplete HTTP body"};
    }
    return HttpResponse{status, std::string{body}};
}

class NativeTransport final : public Transport {
public:
    HttpResult post(const HttpRequest& request,
                    CancellationToken cancellation) override {
#ifdef _WIN32
        static const Winsock winsock;
        if (!winsock.ready()) {
            return TransportError{TransportErrorCode::ConnectionFailure,
                                  "Windows sockets could not be initialized"};
        }
#endif
        if (request.timeout <= std::chrono::milliseconds::zero()) {
            return TransportError{TransportErrorCode::Timeout,
                                  "the Ollama request timeout must be positive"};
        }
        auto parsed = parse_url(request.url);
        if (const auto* error = std::get_if<TransportError>(&parsed)) return *error;
        const ParsedUrl& url = std::get<ParsedUrl>(parsed);
        const auto deadline = std::chrono::steady_clock::now() + request.timeout;
        auto connection = connect_to(url, deadline, cancellation);
        if (const auto* error = std::get_if<TransportError>(&connection)) return *error;
        Socket socket = std::get<Socket>(std::move(connection));

        std::ostringstream wire;
        wire << "POST " << url.path << " HTTP/1.1\r\n"
             << "Host: " << url.authority << "\r\n"
             << "Content-Type: application/json\r\n"
             << "Accept: application/json\r\n"
             << "Content-Length: " << request.body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << request.body;
        if (const auto error = send_all(socket.get(), wire.str(), deadline, cancellation)) {
            return *error;
        }
        auto response = receive_all(socket.get(), deadline, cancellation);
        if (const auto* error = std::get_if<TransportError>(&response)) return *error;
        return parse_http_response(std::get<std::string>(response));
    }
};

} // namespace

std::shared_ptr<Transport> make_native_transport() {
    return std::make_shared<NativeTransport>();
}

} // namespace horusrf::ai::ollama
