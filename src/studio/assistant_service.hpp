// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "horusrf/ai/contracts.hpp"

#include <QObject>
#include <QString>

#include <chrono>
#include <memory>
#include <stop_token>
#include <thread>
#include <vector>

namespace horusrf::studio {

enum class AssistantMode { Generate, Modify };

struct AssistantRequest final {
    AssistantMode mode{AssistantMode::Generate};
    std::string instruction;
    std::string current_source;
    std::string endpoint;
    std::string model;
    std::chrono::milliseconds timeout{std::chrono::minutes{2}};
};

struct ModelDiscoveryResult final {
    std::vector<std::string> models;
    std::string error;

    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

class AssistantBackend {
public:
    virtual ~AssistantBackend() = default;
    [[nodiscard]] virtual ai::AgentResult run(
        const AssistantRequest& request, std::stop_token cancellation) = 0;
    [[nodiscard]] virtual ModelDiscoveryResult discoverModels(
        const std::string& endpoint, std::chrono::milliseconds timeout,
        std::stop_token cancellation) = 0;
};

[[nodiscard]] std::shared_ptr<AssistantBackend> makeOllamaAssistantBackend();

class AssistantService final : public QObject {
    Q_OBJECT

public:
    explicit AssistantService(std::shared_ptr<AssistantBackend> backend,
                              QObject* parent = nullptr);
    ~AssistantService() override;

    [[nodiscard]] bool startRequest(AssistantRequest request);
    [[nodiscard]] bool discoverModels(std::string endpoint,
                                      std::chrono::milliseconds timeout);
    [[nodiscard]] bool isRunning() const noexcept { return active_; }
    [[nodiscard]] std::shared_ptr<const ai::AgentResult> lastResult() const {
        return last_result_;
    }
    [[nodiscard]] std::shared_ptr<const ModelDiscoveryResult> lastDiscovery() const {
        return last_discovery_;
    }

    void cancel();

signals:
    void requestStarted();
    void requestFinished();
    void discoveryStarted();
    void discoveryFinished();

private:
    void prepareWorker();

    std::shared_ptr<AssistantBackend> backend_;
    std::jthread worker_;
    std::shared_ptr<const ai::AgentResult> last_result_;
    std::shared_ptr<const ModelDiscoveryResult> last_discovery_;
    bool active_{};
};

} // namespace horusrf::studio
