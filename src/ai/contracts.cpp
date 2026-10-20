#include "horusrf/ai/contracts.hpp"

#include <stdexcept>
#include <utility>

namespace horusrf::ai {

ValidationResult::ValidationResult(
    bool valid, std::vector<ValidationDiagnostic> diagnostics)
    : valid_(valid), diagnostics_(std::move(diagnostics)) {}

ValidationResult ValidationResult::valid() { return ValidationResult{true, {}}; }

ValidationResult ValidationResult::invalid(
    std::vector<ValidationDiagnostic> diagnostics) {
    if (diagnostics.empty()) {
        throw std::invalid_argument("an invalid validation result requires diagnostics");
    }
    return ValidationResult{false, std::move(diagnostics)};
}

RepairAttempt::RepairAttempt(std::size_t number, ScriptSource candidate_source,
                             ValidationResult validation, ProviderIdentity provider)
    : number_(number),
      candidate_source_(std::move(candidate_source)),
      validation_(std::move(validation)),
      provider_(std::move(provider)) {
    if (number_ == 0) {
        throw std::invalid_argument("a repair attempt number must be positive");
    }
}

ScriptProposal::ScriptProposal(ScriptSource candidate_source,
                               ValidationResult validation,
                               std::string explanation,
                               std::size_t attempt_count,
                               ProviderIdentity provider)
    : candidate_source_(std::move(candidate_source)),
      validation_(std::move(validation)),
      explanation_(std::move(explanation)),
      attempt_count_(attempt_count),
      provider_(std::move(provider)) {
    if (attempt_count_ == 0) {
        throw std::invalid_argument("a script proposal requires at least one attempt");
    }
}

} // namespace horusrf::ai
