#include "test_support.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ai/fake_provider.hpp"
#include "horusrf/ai/ollama_provider.hpp"

namespace {

using namespace horusrf;

class FakeTransport final : public ai::ollama::Transport {
public:
    explicit FakeTransport(std::vector<ai::ollama::HttpResult> results)
        : results_(std::move(results)) {}

    ai::ollama::HttpResult post(const ai::ollama::HttpRequest& request,
                                ai::CancellationToken cancellation) override {
        return next(request, cancellation, false);
    }

    ai::ollama::HttpResult get(const ai::ollama::HttpRequest& request,
                               ai::CancellationToken cancellation) override {
        return next(request, cancellation, true);
    }

    std::vector<ai::ollama::HttpRequest> requests;
    bool saw_cancellation{};
    bool saw_get{};

private:
    ai::ollama::HttpResult next(const ai::ollama::HttpRequest& request,
                                ai::CancellationToken cancellation, bool is_get) {
        requests.push_back(request);
        saw_cancellation = cancellation.stop_requested();
        saw_get = saw_get || is_get;
        if (next_ >= results_.size()) {
            return ai::ollama::TransportError{
                ai::ollama::TransportErrorCode::ConnectionFailure,
                "unexpected fake transport call"};
        }
        return results_[next_++];
    }

    std::vector<ai::ollama::HttpResult> results_;
    std::size_t next_{};
};

class ThrowingTransport final : public ai::ollama::Transport {
public:
    ai::ollama::HttpResult post(const ai::ollama::HttpRequest&,
                                ai::CancellationToken) override {
        throw std::runtime_error("injected transport exception");
    }
};

ai::LlmRequest request() {
    return {
        {{ai::LlmRole::System, "Return HorusRF DSL only."},
         {ai::LlmRole::User, "Sweep 1 \"GHz\" to 2 GHz.\nNo prose."}},
        "qwen2.5-coder:7b",
        0.1,
        1536,
        std::chrono::milliseconds{4321},
        std::string{"{\"type\":\"object\",\"required\":[\"source\"]}"},
    };
}

const ai::ProviderError& error_from(const ai::LlmResult& result) {
    CHECK(std::holds_alternative<ai::ProviderError>(result));
    return std::get<ai::ProviderError>(result);
}

void request_encoding_and_success_test() {
    auto transport = std::make_shared<FakeTransport>(
        std::vector<ai::ollama::HttpResult>{ai::ollama::HttpResponse{
            200,
            R"({"model":"qwen2.5-coder:7b","message":{"role":"assistant","content":"characterize caf\u00e9 {}"},"done":true,"done_reason":"stop","load_duration":1000000,"prompt_eval_duration":2000000,"eval_duration":4000000,"prompt_eval_count":120,"eval_count":40})"}});
    ai::OllamaProvider provider{{"http://127.0.0.1:11434/"}, transport};
    const ai::LlmResult result = provider.generate(request());

    CHECK(std::holds_alternative<ai::ProviderResponse>(result));
    const auto& response = std::get<ai::ProviderResponse>(result);
    CHECK_EQ(response.text, "characterize caf\xc3\xa9 {}");
    CHECK_EQ(response.identity.provider, "ollama");
    CHECK_EQ(response.identity.model, "qwen2.5-coder:7b");
    CHECK(response.metrics.has_value());
    CHECK_EQ(response.metrics->model_load_duration,
             std::optional{std::chrono::nanoseconds{1000000}});
    CHECK_EQ(response.metrics->prompt_evaluation_duration,
             std::optional{std::chrono::nanoseconds{2000000}});
    CHECK_EQ(response.metrics->generation_duration,
             std::optional{std::chrono::nanoseconds{4000000}});
    CHECK_EQ(response.metrics->input_token_count, std::optional<std::size_t>{120});
    CHECK_EQ(response.metrics->output_token_count, std::optional<std::size_t>{40});
    CHECK_EQ(transport->requests.size(), std::size_t{1});
    CHECK_EQ(transport->requests.front().url, "http://127.0.0.1:11434/api/chat");
    CHECK_EQ(transport->requests.front().timeout, std::chrono::milliseconds{4321});
    const std::string& body = transport->requests.front().body;
    CHECK(body.find("\"stream\":false") != std::string::npos);
    CHECK(body.find("\"role\":\"system\"") != std::string::npos);
    CHECK(body.find("Sweep 1 \\\"GHz\\\" to 2 GHz.\\nNo prose.") != std::string::npos);
    CHECK(body.find("\"temperature\":0.1") != std::string::npos);
    CHECK(body.find("\"num_predict\":1536") != std::string::npos);
    CHECK(body.find("\"format\":{\"type\":\"object\"") != std::string::npos);
}

void request_validation_test() {
    auto transport = std::make_shared<FakeTransport>(
        std::vector<ai::ollama::HttpResult>{});
    ai::OllamaProvider provider{{}, transport};

    auto candidate = request();
    candidate.messages.clear();
    CHECK_EQ(error_from(provider.generate(candidate)).code,
             ai::ProviderErrorCode::InvalidRequest);
    candidate = request();
    candidate.model.clear();
    CHECK_EQ(error_from(provider.generate(candidate)).code,
             ai::ProviderErrorCode::InvalidRequest);
    candidate = request();
    candidate.timeout = std::chrono::milliseconds::zero();
    CHECK_EQ(error_from(provider.generate(candidate)).code,
             ai::ProviderErrorCode::InvalidRequest);
    candidate = request();
    candidate.json_schema = "{broken";
    CHECK_EQ(error_from(provider.generate(candidate)).code,
             ai::ProviderErrorCode::InvalidRequest);

    ai::OllamaProvider secure_endpoint{{"https://127.0.0.1:11434"}, transport};
    CHECK_EQ(error_from(secure_endpoint.generate(request())).code,
             ai::ProviderErrorCode::InvalidEndpoint);
    CHECK(transport->requests.empty());
}

void provider_response_error_test() {
    auto transport = std::make_shared<FakeTransport>(
        std::vector<ai::ollama::HttpResult>{
            ai::ollama::HttpResponse{404, R"({"error":"model 'missing' not found"})"},
            ai::ollama::HttpResponse{400, R"({"error":"context length exceeded"})"},
            ai::ollama::HttpResponse{200, "not-json"},
            ai::ollama::HttpResponse{200, R"({"model":"qwen","message":{}})"},
            ai::ollama::HttpResponse{200, R"({"model":"qwen","message":{"content":" \n\t"},"done":true})"},
            ai::ollama::HttpResponse{200, R"({"model":"qwen","message":{"content":"partial"},"done":true,"done_reason":"length"})"},
            ai::ollama::HttpResponse{200, R"({"model":"qwen","message":{"content":"partial"},"done":false})"},
        });
    ai::OllamaProvider provider{{}, transport};
    CHECK_EQ(error_from(provider.generate(request())).code,
             ai::ProviderErrorCode::ModelUnavailable);
    CHECK_EQ(error_from(provider.generate(request())).code,
             ai::ProviderErrorCode::ContextLimit);
    CHECK_EQ(error_from(provider.generate(request())).code,
             ai::ProviderErrorCode::MalformedResponse);
    CHECK_EQ(error_from(provider.generate(request())).code,
             ai::ProviderErrorCode::MalformedResponse);
    CHECK_EQ(error_from(provider.generate(request())).code,
             ai::ProviderErrorCode::EmptyResponse);
    CHECK_EQ(error_from(provider.generate(request())).code,
             ai::ProviderErrorCode::OutputLimit);
    CHECK_EQ(error_from(provider.generate(request())).code,
             ai::ProviderErrorCode::UnexpectedResponse);
}

void transport_error_mapping_test() {
    using Code = ai::ollama::TransportErrorCode;
    auto transport = std::make_shared<FakeTransport>(
        std::vector<ai::ollama::HttpResult>{
            ai::ollama::TransportError{Code::InvalidEndpoint, "bad endpoint"},
            ai::ollama::TransportError{Code::ConnectionFailure, "not running"},
            ai::ollama::TransportError{Code::Timeout, "too slow"},
            ai::ollama::TransportError{Code::Cancelled, "cancelled"},
            ai::ollama::TransportError{Code::InvalidResponse, "bad HTTP"},
        });
    ai::OllamaProvider provider{{}, transport};
    CHECK_EQ(error_from(provider.generate(request())).code,
             ai::ProviderErrorCode::InvalidEndpoint);
    CHECK_EQ(error_from(provider.generate(request())).code,
             ai::ProviderErrorCode::Unavailable);
    CHECK_EQ(error_from(provider.generate(request())).code,
             ai::ProviderErrorCode::Timeout);
    CHECK_EQ(error_from(provider.generate(request())).code,
             ai::ProviderErrorCode::Cancelled);
    CHECK_EQ(error_from(provider.generate(request())).code,
             ai::ProviderErrorCode::MalformedResponse);
}

void cancellation_test() {
    auto transport = std::make_shared<FakeTransport>(
        std::vector<ai::ollama::HttpResult>{});
    ai::OllamaProvider provider{{}, transport};
    std::stop_source source;
    source.request_stop();
    CHECK_EQ(error_from(provider.generate(request(), source.get_token())).code,
             ai::ProviderErrorCode::Cancelled);
    CHECK(transport->requests.empty());
}

void model_discovery_test() {
    auto transport = std::make_shared<FakeTransport>(
        std::vector<ai::ollama::HttpResult>{ai::ollama::HttpResponse{
            200, R"({"models":[{"name":"qwen2.5-coder:7b"},{"name":"qwen3:14b"}]})"}});
    ai::OllamaProvider provider{{"http://127.0.0.1:11434/"}, transport};
    const auto result = provider.list_models(std::chrono::milliseconds{2468});
    CHECK(std::holds_alternative<std::vector<std::string>>(result));
    const auto& models = std::get<std::vector<std::string>>(result);
    CHECK_EQ(models.size(), std::size_t{2});
    CHECK_EQ(models[0], "qwen2.5-coder:7b");
    CHECK_EQ(models[1], "qwen3:14b");
    CHECK(transport->saw_get);
    CHECK_EQ(transport->requests.front().url, "http://127.0.0.1:11434/api/tags");
    CHECK_EQ(transport->requests.front().timeout, std::chrono::milliseconds{2468});

    auto malformed_transport = std::make_shared<FakeTransport>(
        std::vector<ai::ollama::HttpResult>{ai::ollama::HttpResponse{
            200, R"({"models":[{"model":"missing-name"}]})"}});
    ai::OllamaProvider malformed{{}, malformed_transport};
    const auto malformed_result = malformed.list_models();
    CHECK_EQ(std::get<ai::ProviderError>(malformed_result).code,
             ai::ProviderErrorCode::MalformedResponse);
}

void transport_exception_test() {
    ai::OllamaProvider provider{{}, std::make_shared<ThrowingTransport>()};
    const ai::LlmResult result = provider.generate(request());
    const auto& error = error_from(result);
    CHECK_EQ(error.code, ai::ProviderErrorCode::UnexpectedResponse);
    CHECK(error.message.find("injected transport exception") != std::string::npos);
}

void scripted_provider_test() {
    ai::testing::FakeLlmProvider provider{{
        ai::ProviderResponse{"first", {"fake", "fixed"}},
        ai::ProviderError{ai::ProviderErrorCode::Timeout, "injected timeout"},
    }};
    const auto first = provider.generate(request());
    CHECK(std::holds_alternative<ai::ProviderResponse>(first));
    CHECK_EQ(std::get<ai::ProviderResponse>(first).text, "first");
    const auto second = provider.generate(request());
    CHECK_EQ(error_from(second).code, ai::ProviderErrorCode::Timeout);
    CHECK_EQ(provider.requests().size(), std::size_t{2});
    CHECK_EQ(provider.remaining(), std::size_t{0});
    CHECK_EQ(error_from(provider.generate(request())).code,
             ai::ProviderErrorCode::UnexpectedResponse);
}

} // namespace

int main() {
    try {
        request_encoding_and_success_test();
        request_validation_test();
        provider_response_error_test();
        transport_error_mapping_test();
        cancellation_test();
        model_discovery_test();
        transport_exception_test();
        scripted_provider_test();
        std::cout << "All HorusRF AI provider tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
