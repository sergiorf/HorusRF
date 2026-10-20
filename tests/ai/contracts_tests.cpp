#include "test_support.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "horusrf/ai/contracts.hpp"

namespace {

using namespace horusrf;

ai::ValidationDiagnostic syntax_diagnostic() {
    return {ai::DiagnosticCategory::Syntax,
            "parse.invalid_token",
            "invalid token '@'",
            {{1, 31, 30}, {1, 32, 31}}};
}

void request_contracts_test() {
    const ai::GenerationRequest generation{
        ai::NaturalLanguageInstruction{"sweep from 1 GHz to 2 GHz"}};
    CHECK_EQ(generation.instruction.text(), "sweep from 1 GHz to 2 GHz");

    const ai::ModificationRequest modification{
        ai::ScriptSource{"characterize current {}"},
        ai::NaturalLanguageInstruction{"add a power measurement"}};
    CHECK_EQ(modification.current_source.text(), "characterize current {}");
    CHECK_EQ(modification.instruction.text(), "add a power measurement");
}

void provider_response_contract_test() {
    const ai::ProviderResponse response{
        "characterize generated {}", {"ollama", "qwen2.5-coder:7b"}};
    CHECK_EQ(response.text, "characterize generated {}");
    CHECK_EQ(response.identity.provider, "ollama");
    CHECK_EQ(response.identity.model, "qwen2.5-coder:7b");
}

void validation_result_contract_test() {
    const auto valid = ai::ValidationResult::valid();
    CHECK(valid.ok());
    CHECK(valid.diagnostics().empty());

    const auto diagnostic = syntax_diagnostic();
    const auto invalid = ai::ValidationResult::invalid({diagnostic});
    CHECK(!invalid.ok());
    CHECK_EQ(invalid.diagnostics().size(), std::size_t{1});
    CHECK_EQ(invalid.diagnostics().front(), diagnostic);

    bool rejected_empty_diagnostics = false;
    try {
        static_cast<void>(
            ai::ValidationResult::invalid(std::vector<ai::ValidationDiagnostic>{}));
    } catch (const std::invalid_argument&) {
        rejected_empty_diagnostics = true;
    }
    CHECK(rejected_empty_diagnostics);
}

void repair_attempt_contract_test() {
    const ai::RepairAttempt attempt{
        2,
        ai::ScriptSource{"characterize repaired {}"},
        ai::ValidationResult::invalid({syntax_diagnostic()}),
        {"ollama", "qwen2.5-coder:7b"},
    };
    CHECK_EQ(attempt.number(), std::size_t{2});
    CHECK_EQ(attempt.candidate_source().text(), "characterize repaired {}");
    CHECK(!attempt.validation().ok());
    CHECK_EQ(attempt.provider().provider, "ollama");

    bool rejected_zero = false;
    try {
        const ai::RepairAttempt invalid_attempt{
            0, ai::ScriptSource{""}, ai::ValidationResult::valid(), {"fake", "fixed"}};
        static_cast<void>(invalid_attempt);
    } catch (const std::invalid_argument&) {
        rejected_zero = true;
    }
    CHECK(rejected_zero);
}

void proposal_contract_test() {
    const ai::ScriptProposal valid_proposal{
        ai::ScriptSource{"characterize proposed {}"},
        ai::ValidationResult::valid(),
        "Generated a characterization.",
        1,
        {"ollama", "qwen2.5-coder:7b"},
    };
    CHECK(valid_proposal.eligible_to_apply());
    CHECK_EQ(valid_proposal.candidate_source().text(), "characterize proposed {}");
    CHECK_EQ(valid_proposal.explanation(), "Generated a characterization.");
    CHECK_EQ(valid_proposal.attempt_count(), std::size_t{1});
    CHECK_EQ(valid_proposal.provider().model, "qwen2.5-coder:7b");
    CHECK(valid_proposal.warnings().empty());

    const ai::ScriptProposal invalid_proposal{
        ai::ScriptSource{"characterize broken { @ }"},
        ai::ValidationResult::invalid({syntax_diagnostic()}),
        "The repair limit was reached.",
        3,
        {"ollama", "qwen2.5-coder:7b"},
        {"review the requested frequency step"},
    };
    CHECK(!invalid_proposal.eligible_to_apply());
    CHECK_EQ(invalid_proposal.validation().diagnostics().size(), std::size_t{1});
    CHECK_EQ(invalid_proposal.warnings().size(), std::size_t{1});

    const ai::AgentResult failed = ai::AgentFailure{
        {ai::AgentErrorCode::RepairLimitReached, "repair limit reached"},
        invalid_proposal};
    CHECK(std::holds_alternative<ai::AgentFailure>(failed));
    CHECK(std::get<ai::AgentFailure>(failed).proposal.has_value());

    static_assert(!std::is_copy_assignable_v<ai::ScriptProposal>);
    static_assert(!std::is_move_assignable_v<ai::ScriptProposal>);
}

void error_and_cancellation_contract_test() {
    const ai::AgentError error{ai::AgentErrorCode::Cancelled,
                               "the user cancelled generation"};
    CHECK_EQ(error.code, ai::AgentErrorCode::Cancelled);
    CHECK_EQ(error.message, "the user cancelled generation");

    static_assert(std::is_same_v<ai::CancellationToken, std::stop_token>);
    std::stop_source source;
    const ai::CancellationToken token = source.get_token();
    CHECK(!token.stop_requested());
    source.request_stop();
    CHECK(token.stop_requested());
}

} // namespace

int main() {
    try {
        request_contracts_test();
        provider_response_contract_test();
        validation_result_contract_test();
        repair_attempt_contract_test();
        proposal_contract_test();
        error_and_cancellation_contract_test();
        std::cout << "All HorusRF AI contract tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
