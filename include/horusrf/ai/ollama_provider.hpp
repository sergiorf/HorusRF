#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "horusrf/ai/provider.hpp"

namespace horusrf::ai::ollama {

struct HttpRequest final {
    std::string url;
    std::string body;
    std::chrono::milliseconds timeout;
};

struct HttpResponse final {
    int status{};
    std::string body;
};

enum class TransportErrorCode {
    InvalidEndpoint,
    ConnectionFailure,
    Timeout,
    Cancelled,
    InvalidResponse,
};

struct TransportError final {
    TransportErrorCode code{TransportErrorCode::ConnectionFailure};
    std::string message;
};

using HttpResult = std::variant<HttpResponse, TransportError>;

// Narrow injection boundary used to test the Ollama protocol without sockets.
class Transport {
public:
    virtual ~Transport() = default;
    [[nodiscard]] virtual HttpResult post(const HttpRequest& request,
                                          CancellationToken cancellation) = 0;
    [[nodiscard]] virtual HttpResult get(const HttpRequest& request,
                                         CancellationToken cancellation);
};

[[nodiscard]] std::shared_ptr<Transport> make_native_transport();

} // namespace horusrf::ai::ollama

namespace horusrf::ai {

struct OllamaConfig final {
    std::string endpoint{"http://127.0.0.1:11434"};
};

using ModelListResult = std::variant<std::vector<std::string>, ProviderError>;

class OllamaProvider final : public LlmProvider {
public:
    explicit OllamaProvider(
        OllamaConfig config = {},
        std::shared_ptr<ollama::Transport> transport = ollama::make_native_transport());

    [[nodiscard]] LlmResult generate(
        const LlmRequest& request, CancellationToken cancellation = {}) override;
    [[nodiscard]] ModelListResult list_models(
        std::chrono::milliseconds timeout = std::chrono::seconds{5},
        CancellationToken cancellation = {});

private:
    [[nodiscard]] LlmResult generate_checked(
        const LlmRequest& request, CancellationToken cancellation);
    [[nodiscard]] ModelListResult list_models_checked(
        std::chrono::milliseconds timeout, CancellationToken cancellation);

    OllamaConfig config_;
    std::shared_ptr<ollama::Transport> transport_;
    std::unordered_map<std::string, std::string> model_digests_;
};

} // namespace horusrf::ai
