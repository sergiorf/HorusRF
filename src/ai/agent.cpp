#include "horusrf/ai/agent.hpp"

#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "horusrf/ai/validation.hpp"
#include "horusrf/ai/requirement_verification.hpp"

namespace horusrf::ai {
namespace {

struct Candidate final {
    ProposalPayload payload;
    ProviderIdentity provider;
};

bool has_text(std::string_view value) {
    return value.find_first_not_of(" \t\r\n") != std::string_view::npos;
}

RequirementVerificationResult requirement_verification(
    const Candidate& candidate, const ValidationResult& validation,
    const NaturalLanguageInstruction& instruction) {
    if (!validation.ok()) return {};
    const auto facts = compile_program_facts(candidate.payload.script.text());
    if (!facts) {
        return {RequirementSatisfactionStatus::Unknown,
                {{"interactive-verification", RequirementOutcomeStatus::Unknown,
                  "program_facts_unavailable", "compiler-backed program facts",
                  "unavailable",
                  "the valid proposal could not be projected for requirement verification"}}};
    }
    return verify_instruction_requirements(instruction.text(), *facts);
}

ScriptProposal proposal_from(const Candidate& candidate,
                             const ValidationResult& validation,
                             std::size_t attempt_count,
                             const NaturalLanguageInstruction& instruction) {
    return {candidate.payload.script, validation, candidate.payload.explanation,
            attempt_count, candidate.provider,
            candidate.payload.warnings,
            requirement_verification(candidate, validation, instruction)};
}

AgentResult failure_without_proposal(AgentErrorCode code, std::string message) {
    return AgentFailure{{code, std::move(message)}, std::nullopt};
}

AgentResult failure_with_proposal(AgentErrorCode code, std::string message,
                                  const Candidate& candidate,
                                  const ValidationResult& validation,
                                  std::size_t attempt_count,
                                  const NaturalLanguageInstruction& instruction) {
    return AgentFailure{
        {code, std::move(message)},
        std::optional<ScriptProposal>{
            std::in_place, proposal_from(candidate, validation, attempt_count, instruction)}};
}

std::string response_error_message(const ProposalResponseError& error) {
    return "the provider returned an unusable proposal: " + error.message;
}

} // namespace

AgentSession::AgentSession(LlmProvider& provider, PromptBuilder prompt_builder,
                           AgentSettings settings)
    : provider_(provider),
      prompt_builder_(std::move(prompt_builder)),
      settings_(settings) {}

AgentResult AgentSession::generate(const GenerationRequest& request,
                                   CancellationToken cancellation) {
    try {
        return run(request.instruction, std::nullopt,
                   prompt_builder_.build_generation(request), cancellation);
    } catch (const std::invalid_argument& error) {
        state_ = AgentState::Failed;
        return failure_without_proposal(AgentErrorCode::InvalidRequest, error.what());
    } catch (const std::exception& error) {
        state_ = AgentState::Failed;
        return failure_without_proposal(AgentErrorCode::Internal, error.what());
    } catch (...) {
        state_ = AgentState::Failed;
        return failure_without_proposal(AgentErrorCode::Internal,
                                        "unexpected non-standard agent failure");
    }
}

AgentResult AgentSession::modify(const ModificationRequest& request,
                                 CancellationToken cancellation) {
    try {
        return run(request.instruction, request.current_source,
                   prompt_builder_.build_modification(request), cancellation);
    } catch (const std::invalid_argument& error) {
        state_ = AgentState::Failed;
        return failure_without_proposal(AgentErrorCode::InvalidRequest, error.what());
    } catch (const std::exception& error) {
        state_ = AgentState::Failed;
        return failure_without_proposal(AgentErrorCode::Internal, error.what());
    } catch (...) {
        state_ = AgentState::Failed;
        return failure_without_proposal(AgentErrorCode::Internal,
                                        "unexpected non-standard agent failure");
    }
}

AgentResult AgentSession::run(const NaturalLanguageInstruction& instruction,
                              std::optional<ScriptSource> original_source,
                              LlmRequest request, CancellationToken cancellation) {
    std::optional<Candidate> previous;
    std::optional<ValidationResult> previous_validation;
    std::size_t attempt_count = 0;

    const auto fail = [&](AgentErrorCode code, std::string message) -> AgentResult {
        if (previous && previous_validation) {
            return failure_with_proposal(code, std::move(message), *previous,
                                         *previous_validation, attempt_count,
                                         instruction);
        }
        return failure_without_proposal(code, std::move(message));
    };

    for (;;) {
        if (cancellation.stop_requested()) {
            state_ = AgentState::Cancelled;
            return fail(AgentErrorCode::Cancelled,
                        "the authoring request was cancelled");
        }

        state_ = previous ? AgentState::Repairing : AgentState::Generating;
        LlmResult provider_result;
        try {
            provider_result = provider_.generate(request, cancellation);
        } catch (const std::exception& error) {
            state_ = AgentState::Failed;
            return fail(
                AgentErrorCode::ProviderFailure,
                "the provider threw an exception: " + std::string{error.what()});
        } catch (...) {
            state_ = AgentState::Failed;
            return fail(
                AgentErrorCode::ProviderFailure,
                "the provider threw a non-standard exception");
        }

        if (const auto* error = std::get_if<ProviderError>(&provider_result)) {
            if (error->code == ProviderErrorCode::Cancelled || cancellation.stop_requested()) {
                state_ = AgentState::Cancelled;
                return fail(AgentErrorCode::Cancelled, error->message);
            }
            state_ = AgentState::Failed;
            return fail(AgentErrorCode::ProviderFailure, error->message);
        }
        if (cancellation.stop_requested()) {
            state_ = AgentState::Cancelled;
            return fail(AgentErrorCode::Cancelled,
                        "the authoring request was cancelled");
        }

        auto response = parse_proposal_response(
            std::get<ProviderResponse>(provider_result).text);
        if (const auto* error = std::get_if<ProposalResponseError>(&response)) {
            state_ = AgentState::Failed;
            return fail(AgentErrorCode::ProviderFailure,
                        response_error_message(*error));
        }
        auto payload = std::get<ProposalPayload>(std::move(response));
        if (!has_text(payload.script.text())) {
            state_ = AgentState::Failed;
            return fail(AgentErrorCode::ProviderFailure,
                        "the provider returned an empty script");
        }

        Candidate candidate{std::move(payload),
                            std::get<ProviderResponse>(provider_result).identity};
        ++attempt_count;
        if (previous && candidate.payload.script == previous->payload.script) {
            state_ = AgentState::Failed;
            return failure_with_proposal(
                AgentErrorCode::RepairLimitReached,
                "repair stopped because the provider returned the unchanged invalid script",
                candidate, *previous_validation, attempt_count, instruction);
        }

        if (cancellation.stop_requested()) {
            state_ = AgentState::Cancelled;
            return fail(AgentErrorCode::Cancelled,
                        "the authoring request was cancelled");
        }

        state_ = AgentState::Validating;
        ValidationResult validation = ValidationResult::valid();
        try {
            validation = validate_script(candidate.payload.script.text());
        } catch (const std::exception& error) {
            state_ = AgentState::Failed;
            return failure_without_proposal(
                AgentErrorCode::ValidationFailure,
                "compiler validation failed unexpectedly: " + std::string{error.what()});
        } catch (...) {
            state_ = AgentState::Failed;
            return failure_without_proposal(
                AgentErrorCode::ValidationFailure,
                "compiler validation failed with a non-standard exception");
        }

        if (validation.ok()) {
            state_ = AgentState::Completed;
            return proposal_from(candidate, validation, attempt_count, instruction);
        }

        if (attempt_count > settings_.maximum_repair_attempts) {
            state_ = AgentState::Failed;
            return failure_with_proposal(
                AgentErrorCode::RepairLimitReached,
                "the maximum number of repair attempts was reached", candidate,
                validation, attempt_count, instruction);
        }

        previous = std::move(candidate);
        previous_validation = std::move(validation);
        request = prompt_builder_.build_repair(
            {instruction, previous->payload.script, *previous_validation,
             original_source});
    }
}

} // namespace horusrf::ai
