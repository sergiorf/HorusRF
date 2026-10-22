#pragma once

#include <atomic>
#include <cstddef>
#include <optional>

#include "horusrf/ai/contracts.hpp"
#include "horusrf/ai/prompt.hpp"
#include "horusrf/ai/provider.hpp"

namespace horusrf::ai {

enum class AgentState {
    Idle,
    Generating,
    Validating,
    Repairing,
    CorrectingRequirements,
    Completed,
    Failed,
    Cancelled
};

struct AgentSettings final {
    std::size_t maximum_repair_attempts{3};
};

// Synchronous, Qt-free orchestration. UI callers must run a session on a worker
// thread because provider calls may block up to the request timeout.
class AgentSession final {
public:
    explicit AgentSession(LlmProvider& provider,
                          PromptBuilder prompt_builder = PromptBuilder{},
                          AgentSettings settings = {});

    [[nodiscard]] AgentResult generate(
        const GenerationRequest& request, CancellationToken cancellation = {});
    [[nodiscard]] AgentResult modify(
        const ModificationRequest& request, CancellationToken cancellation = {});

    [[nodiscard]] AgentState state() const noexcept {
        return state_.load(std::memory_order_relaxed);
    }

private:
    [[nodiscard]] AgentResult run(
        const NaturalLanguageInstruction& instruction,
        std::optional<ScriptSource> original_source, LlmRequest initial_request,
        CancellationToken cancellation);

    LlmProvider& provider_;
    PromptBuilder prompt_builder_;
    AgentSettings settings_;
    std::atomic<AgentState> state_{AgentState::Idle};
};

} // namespace horusrf::ai
