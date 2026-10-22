#include "horusrf/ai/agent.hpp"

#include <algorithm>
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

std::vector<std::string> diagnostic_set(const ValidationResult& validation) {
    std::vector<std::string> result;
    result.reserve(validation.diagnostics().size());
    for (const auto& diagnostic : validation.diagnostics()) {
        result.push_back(diagnostic.code + "\n" + diagnostic.message + "\n" +
                         std::to_string(diagnostic.span.begin.line) + ":" +
                         std::to_string(diagnostic.span.begin.column) + "-" +
                         std::to_string(diagnostic.span.end.line) + ":" +
                         std::to_string(diagnostic.span.end.column));
    }
    std::ranges::sort(result);
    return result;
}

std::vector<std::string> high_confidence_violation_set(
    const RequirementVerificationResult& verification) {
    std::vector<std::string> result;
    for (const auto& outcome : verification.outcomes) {
        if (outcome.status == RequirementOutcomeStatus::Violated &&
            outcome.confidence == RequirementConfidence::High) {
            result.push_back(outcome.requirement_id + "\n" + outcome.code + "\n" +
                             outcome.expected + "\n" + outcome.actual);
        }
    }
    std::ranges::sort(result);
    return result;
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
    std::optional<RequirementVerificationResult> previous_requirement_verification;
    std::vector<std::string> seen_candidates;
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

        switch (request.purpose) {
        case LlmCallPurpose::Generation: state_ = AgentState::Generating; break;
        case LlmCallPurpose::CompilerRepair: state_ = AgentState::Repairing; break;
        case LlmCallPurpose::RequirementCorrection:
            state_ = AgentState::CorrectingRequirements;
            break;
        }
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

        const bool repeated_candidate = std::ranges::find(
            seen_candidates, candidate.payload.script.text()) != seen_candidates.end();
        seen_candidates.push_back(candidate.payload.script.text());
        if (repeated_candidate) {
            state_ = AgentState::Failed;
            return failure_with_proposal(
                AgentErrorCode::NoProgress,
                "correction stopped with no progress because the provider repeated a candidate",
                candidate, validation, attempt_count, instruction);
        }

        if (!validation.ok()) {
            if (previous_validation && !previous_validation->ok() &&
                diagnostic_set(validation) == diagnostic_set(*previous_validation)) {
                state_ = AgentState::Failed;
                return failure_with_proposal(
                    AgentErrorCode::NoProgress,
                    "compiler repair stopped with no progress because the diagnostic set did not change",
                    candidate, validation, attempt_count, instruction);
            }

            if (attempt_count > settings_.maximum_repair_attempts) {
                state_ = AgentState::Failed;
                return failure_with_proposal(
                    AgentErrorCode::RepairLimitReached,
                    "the shared compiler-repair and requirement-correction call limit was reached",
                    candidate, validation, attempt_count, instruction);
            }

            previous = std::move(candidate);
            previous_validation = std::move(validation);
            previous_requirement_verification.reset();
            request = prompt_builder_.build_repair(
                {instruction, previous->payload.script, *previous_validation,
                 original_source});
            continue;
        }

        const auto verification = requirement_verification(candidate, validation, instruction);
        const auto violations = high_confidence_violation_set(verification);
        if (violations.empty()) {
            state_ = AgentState::Completed;
            return proposal_from(candidate, validation, attempt_count, instruction);
        }

        if (previous_requirement_verification &&
            violations == high_confidence_violation_set(*previous_requirement_verification)) {
            state_ = AgentState::Failed;
            return failure_with_proposal(
                AgentErrorCode::NoProgress,
                "requirement correction stopped with no progress because the verified violations did not change",
                candidate, validation, attempt_count, instruction);
        }

        if (attempt_count > settings_.maximum_repair_attempts) {
            state_ = AgentState::Failed;
            return failure_with_proposal(
                AgentErrorCode::RepairLimitReached,
                "the shared compiler-repair and requirement-correction call limit was reached", candidate,
                validation, attempt_count, instruction);
        }

        previous = std::move(candidate);
        previous_validation = std::move(validation);
        previous_requirement_verification = verification;
        request = prompt_builder_.build_requirement_correction(
            {instruction, previous->payload.script, verification, original_source});
    }
}

} // namespace horusrf::ai
