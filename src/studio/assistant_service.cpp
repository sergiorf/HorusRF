// SPDX-License-Identifier: GPL-3.0-only
#include "assistant_service.hpp"

#include "horusrf/ai/agent.hpp"
#include "horusrf/ai/ollama_provider.hpp"
#include "horusrf/ai/prompt.hpp"
#include "horusrf/ai/retrieval.hpp"

#include <QCoreApplication>
#include <QMetaObject>

#include <filesystem>
#include <sstream>
#include <utility>
#include <variant>

namespace horusrf::studio {
namespace {

class OllamaAssistantBackend final : public AssistantBackend {
public:
    explicit OllamaAssistantBackend(std::filesystem::path retrieval_root)
        : retrieval_root_(std::move(retrieval_root)) {}

    ai::AgentResult run(const AssistantRequest& request,
                        std::stop_token cancellation) override {
        if (!loadRetriever()) {
            return ai::AgentFailure{
                {ai::AgentErrorCode::InvalidRequest,
                 "Studio structural retrieval is unavailable: " + retrieval_error_},
                std::nullopt};
        }
        ai::OllamaProvider provider{{request.endpoint}};
        ai::PromptSettings prompt_settings;
        prompt_settings.model = request.model;
        prompt_settings.timeout = request.timeout;
        prompt_settings.configuration = ai::PromptConfiguration::StructuralRag;
        prompt_settings.retriever = retriever_;
        prompt_settings.retrieval_settings.top_k = 3;
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

private:
    bool loadRetriever() {
        if (retrieval_load_attempted_) return retriever_ != nullptr;
        retrieval_load_attempted_ = true;
        auto loaded = ai::load_retrieval_corpus(retrieval_root_);
        if (loaded.ok()) {
            retriever_ = std::make_shared<const ai::ExampleRetriever>(
                std::move(*loaded.corpus));
            return true;
        }
        std::ostringstream message;
        message << "could not load the compiler-validated corpus at "
                << retrieval_root_.string();
        for (const auto& diagnostic : loaded.diagnostics) {
            message << "; " << diagnostic.path.string() << ": "
                    << diagnostic.message;
        }
        retrieval_error_ = message.str();
        return false;
    }

    std::filesystem::path retrieval_root_;
    std::shared_ptr<const ai::ExampleRetriever> retriever_;
    std::string retrieval_error_;
    bool retrieval_load_attempted_{};
};

} // namespace

std::shared_ptr<AssistantBackend> makeOllamaAssistantBackend() {
    const auto application_directory = std::filesystem::path{
        QCoreApplication::applicationDirPath().toStdString()};
    return std::make_shared<OllamaAssistantBackend>(
        application_directory / "ai" / "retrieval");
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
