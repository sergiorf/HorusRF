#include "horusrf/ai/ollama_provider.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace horusrf::ai {
namespace {

struct JsonValue final {
    enum class Kind { Null, Boolean, Number, String, Array, Object };

    Kind kind{Kind::Null};
    bool boolean{};
    double number{};
    std::string string;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue, std::less<>> object;
};

class JsonParser final {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    JsonValue parse() {
        skip_space();
        JsonValue value = parse_value();
        skip_space();
        if (position_ != input_.size()) fail("unexpected trailing content");
        return value;
    }

private:
    [[noreturn]] void fail(const char* message) const {
        throw std::runtime_error(std::string{message} + " at byte " +
                                 std::to_string(position_));
    }

    void skip_space() {
        while (position_ < input_.size()) {
            const char value = input_[position_];
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n') break;
            ++position_;
        }
    }

    bool consume(char expected) {
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    JsonValue parse_value() {
        if (position_ >= input_.size()) fail("expected a JSON value");
        switch (input_[position_]) {
        case 'n': return parse_literal("null", JsonValue{});
        case 't': {
            JsonValue value;
            value.kind = JsonValue::Kind::Boolean;
            value.boolean = true;
            return parse_literal("true", std::move(value));
        }
        case 'f': {
            JsonValue value;
            value.kind = JsonValue::Kind::Boolean;
            return parse_literal("false", std::move(value));
        }
        case '"': {
            JsonValue value;
            value.kind = JsonValue::Kind::String;
            value.string = parse_string();
            return value;
        }
        case '[': return parse_array();
        case '{': return parse_object();
        default: return parse_number();
        }
    }

    JsonValue parse_literal(std::string_view literal, JsonValue value) {
        if (input_.substr(position_, literal.size()) != literal) fail("invalid literal");
        position_ += literal.size();
        return value;
    }

    static void append_utf8(std::string& output, std::uint32_t codepoint) {
        if (codepoint <= 0x7f) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ff) {
            output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else if (codepoint <= 0xffff) {
            output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else {
            output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    }

    std::uint32_t parse_hex_quad() {
        if (position_ + 4 > input_.size()) fail("incomplete unicode escape");
        std::uint32_t value{};
        for (int index = 0; index < 4; ++index) {
            const char digit = input_[position_++];
            value <<= 4;
            if (digit >= '0' && digit <= '9') value += static_cast<unsigned>(digit - '0');
            else if (digit >= 'a' && digit <= 'f') value += static_cast<unsigned>(digit - 'a' + 10);
            else if (digit >= 'A' && digit <= 'F') value += static_cast<unsigned>(digit - 'A' + 10);
            else fail("invalid unicode escape");
        }
        return value;
    }

    std::string parse_string() {
        if (!consume('"')) fail("expected string");
        std::string output;
        while (position_ < input_.size()) {
            const unsigned char value = static_cast<unsigned char>(input_[position_++]);
            if (value == '"') return output;
            if (value < 0x20) fail("unescaped control character");
            if (value != '\\') {
                output.push_back(static_cast<char>(value));
                continue;
            }
            if (position_ >= input_.size()) fail("incomplete escape");
            const char escaped = input_[position_++];
            switch (escaped) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u': {
                std::uint32_t codepoint = parse_hex_quad();
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (position_ + 2 > input_.size() || input_[position_] != '\\' ||
                        input_[position_ + 1] != 'u') {
                        fail("missing low surrogate");
                    }
                    position_ += 2;
                    const std::uint32_t low = parse_hex_quad();
                    if (low < 0xdc00 || low > 0xdfff) fail("invalid low surrogate");
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                    fail("unexpected low surrogate");
                }
                append_utf8(output, codepoint);
                break;
            }
            default: fail("invalid string escape");
            }
        }
        fail("unterminated string");
    }

    JsonValue parse_number() {
        const std::size_t begin = position_;
        if (consume('-') && position_ == input_.size()) fail("incomplete number");
        if (consume('0')) {
            if (position_ < input_.size() && input_[position_] >= '0' &&
                input_[position_] <= '9') {
                fail("leading zero in number");
            }
        } else {
            const std::size_t digits = position_;
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
            if (position_ == digits) fail("expected number");
        }
        if (consume('.')) {
            const std::size_t digits = position_;
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
            if (position_ == digits) fail("incomplete fraction");
        }
        if (position_ < input_.size() &&
            (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() &&
                (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            const std::size_t digits = position_;
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
            if (position_ == digits) fail("incomplete exponent");
        }
        double number{};
        const auto text = input_.substr(begin, position_ - begin);
        const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
        if (result.ec != std::errc{} || !std::isfinite(number)) fail("invalid number");
        JsonValue value;
        value.kind = JsonValue::Kind::Number;
        value.number = number;
        return value;
    }

    JsonValue parse_array() {
        consume('[');
        JsonValue value;
        value.kind = JsonValue::Kind::Array;
        skip_space();
        if (consume(']')) return value;
        for (;;) {
            skip_space();
            value.array.push_back(parse_value());
            skip_space();
            if (consume(']')) return value;
            if (!consume(',')) fail("expected ',' or ']'");
        }
    }

    JsonValue parse_object() {
        consume('{');
        JsonValue value;
        value.kind = JsonValue::Kind::Object;
        skip_space();
        if (consume('}')) return value;
        for (;;) {
            skip_space();
            if (position_ >= input_.size() || input_[position_] != '"') {
                fail("expected object key");
            }
            std::string key = parse_string();
            skip_space();
            if (!consume(':')) fail("expected ':'");
            skip_space();
            const auto [unused, inserted] = value.object.emplace(std::move(key), parse_value());
            static_cast<void>(unused);
            if (!inserted) fail("duplicate object key");
            skip_space();
            if (consume('}')) return value;
            if (!consume(',')) fail("expected ',' or '}'");
        }
    }

    std::string_view input_;
    std::size_t position_{};
};

const JsonValue* member(const JsonValue& object, std::string_view name) {
    if (object.kind != JsonValue::Kind::Object) return nullptr;
    const auto found = object.object.find(name);
    return found == object.object.end() ? nullptr : &found->second;
}

const std::string* string_member(const JsonValue& object, std::string_view name) {
    const JsonValue* value = member(object, name);
    return value != nullptr && value->kind == JsonValue::Kind::String ? &value->string
                                                                     : nullptr;
}

std::optional<double> number_member(const JsonValue& object, std::string_view name) {
    const JsonValue* value = member(object, name);
    if (value == nullptr || value->kind != JsonValue::Kind::Number) return std::nullopt;
    return value->number;
}

std::optional<std::chrono::nanoseconds> duration_member(
    const JsonValue& object, std::string_view name) {
    const auto value = number_member(object, name);
    if (!value || *value < 0.0 || *value > static_cast<double>(INT64_MAX)) {
        return std::nullopt;
    }
    return std::chrono::nanoseconds{static_cast<std::int64_t>(*value)};
}

std::optional<std::size_t> count_member(const JsonValue& object,
                                        std::string_view name) {
    const auto value = number_member(object, name);
    if (!value || *value < 0.0 || std::floor(*value) != *value ||
        *value > static_cast<double>(SIZE_MAX)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(*value);
}

std::string json_escape(std::string_view input) {
    std::ostringstream output;
    output << '"';
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char value : input) {
        switch (value) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (value < 0x20) {
                output << "\\u00" << hex[value >> 4] << hex[value & 0x0f];
            } else {
                output << static_cast<char>(value);
            }
        }
    }
    output << '"';
    return output.str();
}

std::string role_name(LlmRole role) {
    switch (role) {
    case LlmRole::System: return "system";
    case LlmRole::User: return "user";
    case LlmRole::Assistant: return "assistant";
    }
    return "user";
}

std::string make_body(const LlmRequest& request) {
    std::ostringstream body;
    body << "{\"model\":" << json_escape(request.model) << ",\"messages\":[";
    for (std::size_t index = 0; index < request.messages.size(); ++index) {
        if (index != 0) body << ',';
        body << "{\"role\":" << json_escape(role_name(request.messages[index].role))
             << ",\"content\":" << json_escape(request.messages[index].content) << '}';
    }
    body << "],\"stream\":false,\"options\":{\"temperature\":"
         << request.temperature << ",\"num_predict\":" << request.max_output_tokens << '}';
    if (request.json_schema) body << ",\"format\":" << *request.json_schema;
    body << '}';
    return body.str();
}

std::string lower_ascii(std::string text) {
    for (char& value : text) {
        if (value >= 'A' && value <= 'Z') value = static_cast<char>(value - 'A' + 'a');
    }
    return text;
}

ProviderError http_error(int status, std::string message) {
    const std::string normalized = lower_ascii(message);
    if (status == 404 ||
        (normalized.find("model") != std::string::npos &&
         normalized.find("not found") != std::string::npos)) {
        return {ProviderErrorCode::ModelUnavailable, std::move(message)};
    }
    if (normalized.find("context") != std::string::npos &&
        (normalized.find("limit") != std::string::npos ||
         normalized.find("length") != std::string::npos)) {
        return {ProviderErrorCode::ContextLimit, std::move(message)};
    }
    return {ProviderErrorCode::UnexpectedResponse,
            "Ollama returned HTTP " + std::to_string(status) + ": " + message};
}

ProviderError transport_error(const ollama::TransportError& error) {
    switch (error.code) {
    case ollama::TransportErrorCode::InvalidEndpoint:
        return {ProviderErrorCode::InvalidEndpoint, error.message};
    case ollama::TransportErrorCode::ConnectionFailure:
        return {ProviderErrorCode::Unavailable, error.message};
    case ollama::TransportErrorCode::Timeout:
        return {ProviderErrorCode::Timeout, error.message};
    case ollama::TransportErrorCode::Cancelled:
        return {ProviderErrorCode::Cancelled, error.message};
    case ollama::TransportErrorCode::InvalidResponse:
        return {ProviderErrorCode::MalformedResponse, error.message};
    }
    return {ProviderErrorCode::UnexpectedResponse, error.message};
}

ProviderError invalid_request(std::string message) {
    return {ProviderErrorCode::InvalidRequest, std::move(message)};
}

} // namespace

OllamaProvider::OllamaProvider(OllamaConfig config,
                               std::shared_ptr<ollama::Transport> transport)
    : config_(std::move(config)), transport_(std::move(transport)) {}

LlmResult OllamaProvider::generate(const LlmRequest& request,
                                   CancellationToken cancellation) {
    try {
        return generate_checked(request, cancellation);
    } catch (const std::exception& error) {
        return ProviderError{ProviderErrorCode::UnexpectedResponse,
                             std::string{"the Ollama provider failed: "} + error.what()};
    } catch (...) {
        return ProviderError{ProviderErrorCode::UnexpectedResponse,
                             "the Ollama provider failed unexpectedly"};
    }
}

ModelListResult OllamaProvider::list_models(std::chrono::milliseconds timeout,
                                            CancellationToken cancellation) {
    try {
        return list_models_checked(timeout, cancellation);
    } catch (const std::exception& error) {
        return ProviderError{ProviderErrorCode::UnexpectedResponse,
                             std::string{"the Ollama provider failed: "} + error.what()};
    } catch (...) {
        return ProviderError{ProviderErrorCode::UnexpectedResponse,
                             "the Ollama provider failed unexpectedly"};
    }
}

ModelListResult OllamaProvider::list_models_checked(
    std::chrono::milliseconds timeout, CancellationToken cancellation) {
    if (cancellation.stop_requested()) {
        return ProviderError{ProviderErrorCode::Cancelled,
                             "the Ollama request was cancelled"};
    }
    if (!transport_) return invalid_request("the Ollama transport is not configured");
    if (timeout <= std::chrono::milliseconds::zero()) {
        return invalid_request("request timeout must be positive");
    }
    if (!config_.endpoint.starts_with("http://")) {
        return ProviderError{ProviderErrorCode::InvalidEndpoint,
                             "the Ollama endpoint must use local HTTP"};
    }

    std::string endpoint = config_.endpoint;
    while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
    const ollama::HttpResult http_result = transport_->get(
        {endpoint + "/api/tags", {}, timeout}, cancellation);
    if (const auto* error = std::get_if<ollama::TransportError>(&http_result)) {
        return transport_error(*error);
    }
    const auto& response = std::get<ollama::HttpResponse>(http_result);
    JsonValue root;
    try {
        root = JsonParser{response.body}.parse();
    } catch (const std::exception& error) {
        return ProviderError{ProviderErrorCode::MalformedResponse,
                             std::string{"Ollama returned malformed JSON: "} + error.what()};
    }
    if (response.status < 200 || response.status >= 300) {
        const std::string* error = string_member(root, "error");
        return http_error(response.status,
                          error != nullptr ? *error : "the response contained no error message");
    }
    const JsonValue* models = member(root, "models");
    if (root.kind != JsonValue::Kind::Object || models == nullptr ||
        models->kind != JsonValue::Kind::Array) {
        return ProviderError{ProviderErrorCode::MalformedResponse,
                             "the Ollama model response is missing the models array"};
    }
    std::vector<std::string> names;
    names.reserve(models->array.size());
    for (const auto& model : models->array) {
        const std::string* name = string_member(model, "name");
        if (name == nullptr || name->empty()) {
            return ProviderError{ProviderErrorCode::MalformedResponse,
                                 "an Ollama model entry is missing its name"};
        }
        names.push_back(*name);
    }
    return names;
}

LlmResult OllamaProvider::generate_checked(const LlmRequest& request,
                                           CancellationToken cancellation) {
    if (cancellation.stop_requested()) {
        return ProviderError{ProviderErrorCode::Cancelled,
                             "the Ollama request was cancelled"};
    }
    if (!transport_) return invalid_request("the Ollama transport is not configured");
    if (request.messages.empty()) return invalid_request("an LLM request requires messages");
    for (const auto& message : request.messages) {
        if (message.content.empty()) return invalid_request("LLM messages must not be empty");
    }
    if (request.model.empty()) return invalid_request("an Ollama model must be selected");
    if (!std::isfinite(request.temperature) || request.temperature < 0.0) {
        return invalid_request("temperature must be a finite non-negative value");
    }
    if (request.max_output_tokens == 0) {
        return invalid_request("maximum output tokens must be positive");
    }
    if (request.timeout <= std::chrono::milliseconds::zero()) {
        return invalid_request("request timeout must be positive");
    }
    if (!config_.endpoint.starts_with("http://")) {
        return ProviderError{ProviderErrorCode::InvalidEndpoint,
                             "the Ollama endpoint must use local HTTP"};
    }
    if (request.json_schema) {
        try {
            const JsonValue schema = JsonParser{*request.json_schema}.parse();
            if (schema.kind != JsonValue::Kind::Object) {
                return invalid_request("the structured-output schema must be a JSON object");
            }
        } catch (const std::exception& error) {
            return invalid_request(std::string{"invalid structured-output schema: "} +
                                   error.what());
        }
    }

    std::string endpoint = config_.endpoint;
    while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
    const ollama::HttpRequest http_request{
        endpoint + "/api/chat", make_body(request), request.timeout};
    const ollama::HttpResult http_result = transport_->post(http_request, cancellation);
    if (const auto* error = std::get_if<ollama::TransportError>(&http_result)) {
        return transport_error(*error);
    }

    const auto& response = std::get<ollama::HttpResponse>(http_result);
    JsonValue root;
    try {
        root = JsonParser{response.body}.parse();
    } catch (const std::exception& error) {
        return ProviderError{ProviderErrorCode::MalformedResponse,
                             std::string{"Ollama returned malformed JSON: "} + error.what()};
    }
    if (root.kind != JsonValue::Kind::Object) {
        return ProviderError{ProviderErrorCode::MalformedResponse,
                             "the Ollama response must be a JSON object"};
    }

    if (response.status < 200 || response.status >= 300) {
        const std::string* error = string_member(root, "error");
        return http_error(response.status,
                          error != nullptr ? *error : "the response contained no error message");
    }
    if (const std::string* error = string_member(root, "error")) {
        return ProviderError{ProviderErrorCode::UnexpectedResponse, *error};
    }
    const std::string* model = string_member(root, "model");
    const JsonValue* message = member(root, "message");
    const std::string* content = message != nullptr ? string_member(*message, "content") : nullptr;
    const JsonValue* done = member(root, "done");
    if (model == nullptr || model->empty() || message == nullptr || content == nullptr ||
        done == nullptr || done->kind != JsonValue::Kind::Boolean) {
        return ProviderError{ProviderErrorCode::MalformedResponse,
                             "the Ollama response is missing model, message.content, or done"};
    }
    if (content->find_first_not_of(" \t\r\n") == std::string::npos) {
        return ProviderError{ProviderErrorCode::EmptyResponse,
                             "Ollama returned an empty model response"};
    }
    if (const std::string* reason = string_member(root, "done_reason");
        reason != nullptr && (*reason == "length" || *reason == "max_tokens")) {
        return ProviderError{ProviderErrorCode::OutputLimit,
                             "Ollama stopped because the output limit was reached"};
    }
    if (!done->boolean) {
        return ProviderError{ProviderErrorCode::UnexpectedResponse,
                             "Ollama returned an incomplete non-streaming response"};
    }

    ProviderMetrics metrics{
        duration_member(root, "load_duration"),
        duration_member(root, "prompt_eval_duration"),
        duration_member(root, "eval_duration"),
        count_member(root, "prompt_eval_count"),
        count_member(root, "eval_count"),
    };
    const bool has_metrics = metrics.model_load_duration ||
                             metrics.prompt_evaluation_duration ||
                             metrics.generation_duration || metrics.input_token_count ||
                             metrics.output_token_count;
    return ProviderResponse{*content, {"ollama", *model},
                            has_metrics ? std::optional{metrics} : std::nullopt};
}

} // namespace horusrf::ai
