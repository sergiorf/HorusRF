// SPDX-License-Identifier: GPL-3.0-only
#include "assistant_service.hpp"

#include "horusrf/ai/agent.hpp"
#include "horusrf/ai/ollama_provider.hpp"
#include "horusrf/ai/prompt.hpp"

#include <QMetaObject>

#include <utility>
#include <variant>

namespace horusrf::studio {
namespace {

class OllamaAssistantBackend final : public AssistantBackend {
public:
    ai::AgentResult run(const AssistantRequest& request,
                        std::stop_token cancellation) override {
        ai::OllamaProvider provider{{request.endpoint}};
        ai::PromptSettings prompt_settings;
        prompt_settings.model = request.model;
        prompt_settings.timeout = request.timeout;
        ai::AgentSession session{provider, ai::PromptBuilder{prompt_settings}};
        const ai::NaturalLanguageInstruction instruction{request.instruction};
        if (request.mode == AssistantMode::Modify) {
            return session.modify(
                {ai::ScriptSource{request.current_source}, instruction}, cancellation);
        }
        return session.generate({instruction}, cancellation);
    }

    ModelDiscoveryResult discoverModels(
        const std::string& endpoint, std::chrono::milliseconds timeout,
        std::stop_token cancellation) override {
        ai::OllamaProvider provider{{endpoint}};
        auto result = provider.list_models(timeout, cancellation);
        if (const auto* error = std::get_if<ai::ProviderError>(&result)) {
            return {{}, error->message};
        }
        return {std::get<std::vector<std::string>>(std::move(result)), {}};
    }
};

} // namespace

std::shared_ptr<AssistantBackend> makeOllamaAssistantBackend() {
    return std::make_shared<OllamaAssistantBackend>();
}

AssistantService::AssistantService(std::shared_ptr<AssistantBackend> backend,
                                   QObject* parent)
    : QObject(parent), backend_(std::move(backend)) {}

AssistantService::~AssistantService() {
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
}

void AssistantService::prepareWorker() {
    if (worker_.joinable()) worker_.join();
}

bool AssistantService::startRequest(AssistantRequest request) {
    if (active_ || !backend_) return false;
    prepareWorker();
    active_ = true;
    last_result_.reset();
    emit requestStarted();
    auto backend = backend_;
    worker_ = std::jthread(
        [this, backend = std::move(backend), request = std::move(request)](
            std::stop_token cancellation) mutable {
            auto result = std::make_shared<const ai::AgentResult>(
                backend->run(request, cancellation));
            QMetaObject::invokeMethod(
                this,
                [this, result = std::move(result)] {
                    last_result_ = result;
                    active_ = false;
                    emit requestFinished();
                },
                Qt::QueuedConnection);
        });
    return true;
}

bool AssistantService::discoverModels(std::string endpoint,
                                      std::chrono::milliseconds timeout) {
    if (active_ || !backend_) return false;
    prepareWorker();
    active_ = true;
    last_discovery_.reset();
    emit discoveryStarted();
    auto backend = backend_;
    worker_ = std::jthread(
        [this, backend = std::move(backend), endpoint = std::move(endpoint), timeout](
            std::stop_token cancellation) mutable {
            auto result = std::make_shared<const ModelDiscoveryResult>(
                backend->discoverModels(endpoint, timeout, cancellation));
            QMetaObject::invokeMethod(
                this,
                [this, result = std::move(result)] {
                    last_discovery_ = result;
                    active_ = false;
                    emit discoveryFinished();
                },
                Qt::QueuedConnection);
        });
    return true;
}

void AssistantService::cancel() {
    if (active_ && worker_.joinable()) worker_.request_stop();
}

} // namespace horusrf::studio
