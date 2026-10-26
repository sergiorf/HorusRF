#include "test_support.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ai/fake_provider.hpp"
#include "horusrf/ai/agent.hpp"

namespace {

using namespace horusrf;

constexpr std::string_view valid_source = R"(characterize generated {
  reference power = -10 dBm
  sweep frequency 1 GHz .. 2 GHz step 1 GHz
  measure power
  derive calibration cal {
    correction = reference.power - power
    over frequency
  }
}
)";

constexpr std::string_view invalid_source = R"(characterize generated {
  reference power = -10 dB
  sweep frequency 1 GHz .. 2 GHz step 1 GHz
  measure power
  derive calibration cal {
    correction = reference.power - power
    over frequency
  }
}
)";

std::string json_escape(std::string_view value) {
    std::string result;
    for (const char ch : value) {
        switch (ch) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result.push_back(ch); break;
        }
    }
    return result;
}

ai::ProviderResponse proposal(std::string_view source,
                              std::string_view explanation = "generated",
                              std::string_view warnings = "[]") {
    return {"{\"status\":\"proposal\",\"script\":\"" + json_escape(source) +
                "\",\"explanation\":\"" + json_escape(explanation) +
                "\",\"warnings\":" + std::string{warnings} + "}",
            {"fake", "fixed"}};
}

class CancelRepairProvider final : public ai::LlmProvider {
public:
    explicit CancelRepairProvider(std::stop_source& stop) : stop_(stop) {}

    ai::LlmResult generate(const ai::LlmRequest&,
                           ai::CancellationToken = {}) override {
        ++calls_;
        if (calls_ == 1) return proposal(invalid_source);
        stop_.request_stop();
        return ai::ProviderError{ai::ProviderErrorCode::Cancelled,
                                 "repair was cancelled"};
    }

    std::size_t calls() const noexcept { return calls_; }

private:
    std::stop_source& stop_;
    std::size_t calls_{};
};

const ai::ScriptProposal& completed(const ai::AgentResult& result) {
    CHECK(std::holds_alternative<ai::ScriptProposal>(result));
    return std::get<ai::ScriptProposal>(result);
}

const ai::AgentFailure& failed(const ai::AgentResult& result) {
    CHECK(std::holds_alternative<ai::AgentFailure>(result));
    return std::get<ai::AgentFailure>(result);
}

const ai::UnsupportedRequest& unsupported(const ai::AgentResult& result) {
    CHECK(std::holds_alternative<ai::UnsupportedRequest>(result));
    return std::get<ai::UnsupportedRequest>(result);
}

bool request_contains(const ai::LlmRequest& request, std::string_view text) {
    for (const auto& message : request.messages) {
        if (message.content.find(text) != std::string::npos) return true;
    }
    return false;
}

std::shared_ptr<const ai::ExampleRetriever> fixture_retriever() {
    ai::RetrievalExampleMetadata metadata;
    metadata.id = "agent-reference";
    metadata.category = "agent-test";
    metadata.operations = {"reference_power", "sweep_frequency", "measure_power",
                           "derive_calibration"};
    metadata.structural_steps = {"reference_power", "sweep_frequency",
                                 "measure_power", "derive_calibration"};
    metadata.sweep_dimensions = {"frequency"};
    metadata.comparison_strictness = "none";
    metadata.complexity = "basic";
    ai::RetrievalExample example{
        std::move(metadata),
        "Sweep frequency from 1 GHz to 2 GHz and measure power.",
        std::string{valid_source},
        {},
    };
    ai::RetrievalCorpus corpus{
        "agent-fixture", "1.0.0", "fnv1a64:agent", {std::move(example)}, {}};
    return std::make_shared<const ai::ExampleRetriever>(std::move(corpus));
}

void valid_first_candidate_test() {
    ai::testing::FakeLlmProvider provider{{proposal(valid_source, "valid", "[\"model note\"]")}};
    ai::AgentSession session{provider};
    const auto result = session.generate(
        {ai::NaturalLanguageInstruction{"Create a basic characterization."}});

    const auto& value = completed(result);
    CHECK_EQ(session.state(), ai::AgentState::Completed);
    CHECK(value.eligible_to_apply());
    CHECK_EQ(value.attempt_count(), std::size_t{1});
    CHECK_EQ(value.explanation(), "valid");
    CHECK_EQ(value.warnings().size(), std::size_t{1});
    CHECK_EQ(value.warnings().front(), "model note");
    CHECK_EQ(provider.requests().size(), std::size_t{1});
}

void unsupported_preflight_skips_provider_test() {
    ai::testing::FakeLlmProvider provider{{proposal(valid_source)}};
    ai::AgentSession session{provider};
    const auto result = session.generate({ai::NaturalLanguageInstruction{
        "At a fixed 2.45 GHz, sweep transmit power from -20 dBm to 0 dBm."}});

    const auto& value = unsupported(result);
    CHECK_EQ(session.state(), ai::AgentState::Unsupported);
    CHECK_EQ(value.assessment.status, ai::SupportAssessmentStatus::Unsupported);
    CHECK(!value.assessment.clauses.empty());
    CHECK(provider.requests().empty());
}

void partial_support_is_explicit_on_proposal_test() {
    ai::testing::FakeLlmProvider provider{{proposal(valid_source)}};
    ai::AgentSession session{provider};
    const auto result = session.generate({ai::NaturalLanguageInstruction{
        "Sweep frequency from 1 GHz to 2 GHz and sweep power from -20 dBm to 0 dBm."}});

    const auto& value = completed(result);
    CHECK_EQ(value.support_assessment().status,
             ai::SupportAssessmentStatus::PartiallySupported);
    CHECK(!value.warnings().empty());
    CHECK(value.warnings().back().find("Unsupported request clause") !=
          std::string::npos);
    CHECK_EQ(provider.requests().size(), std::size_t{1});
}

void invalid_candidate_is_repaired_test() {
    ai::testing::FakeLlmProvider provider{{proposal(invalid_source), proposal(valid_source)}};
    ai::AgentSession session{provider};
    const std::string instruction =
        "Use -10 dBm and sweep 1 GHz to 2 GHz in 1 GHz steps.";
    const auto result = session.generate(
        {ai::NaturalLanguageInstruction{instruction}});

    const auto& value = completed(result);
    CHECK_EQ(session.state(), ai::AgentState::Completed);
    CHECK_EQ(value.attempt_count(), std::size_t{2});
    CHECK_EQ(provider.requests().size(), std::size_t{2});
    CHECK(request_contains(provider.requests()[1], instruction));
    CHECK(request_contains(provider.requests()[1], invalid_source));
    CHECK(request_contains(provider.requests()[1], "semantic.unexpected_quantity_type"));
    CHECK(value.warnings().empty());
    CHECK_EQ(value.requirement_verification().status,
             ai::RequirementSatisfactionStatus::Satisfied);
    CHECK(!value.requirement_verification().outcomes.empty());
}

void structural_rag_is_initial_generation_only_test() {
    ai::testing::FakeLlmProvider provider{{proposal(invalid_source), proposal(valid_source)}};
    const ai::PromptBuilder prompts{{
        "qwen2.5-coder:7b", 0.0, 2048, std::chrono::minutes{2},
        ai::PromptConfiguration::StructuralRag, fixture_retriever(),
        {1, 4096, ai::RetrievalBudgetUnit::Utf8Bytes}}};
    ai::AgentSession session{provider, prompts};
    const auto result = session.generate({ai::NaturalLanguageInstruction{
        "Sweep frequency from 1 GHz to 2 GHz and measure power."}});

    CHECK(completed(result).eligible_to_apply());
    CHECK_EQ(provider.requests().size(), std::size_t{2});
    CHECK(provider.requests()[0].retrieval != nullptr);
    CHECK(provider.requests()[0].retrieval->ok());
    CHECK(request_contains(provider.requests()[0], "<retrieved_example"));
    CHECK(provider.requests()[1].retrieval == nullptr);
    CHECK(!request_contains(provider.requests()[1], "<retrieved_example"));
    CHECK_EQ(provider.requests()[1].purpose, ai::LlmCallPurpose::CompilerRepair);

    const std::string corrected =
        std::string{valid_source}.replace(std::string{valid_source}.find("-10 dBm"), 7,
                                          "-5 dBm");
    ai::testing::FakeLlmProvider correction_provider{{proposal(valid_source),
                                                       proposal(corrected)}};
    ai::AgentSession correction_session{correction_provider, prompts};
    const auto correction_result = correction_session.generate(
        {ai::NaturalLanguageInstruction{"Use reference power -5 dBm."}});
    CHECK(completed(correction_result).eligible_to_apply());
    CHECK_EQ(correction_provider.requests().size(), std::size_t{2});
    CHECK(correction_provider.requests()[0].retrieval != nullptr);
    CHECK(correction_provider.requests()[1].retrieval == nullptr);
    CHECK(!request_contains(correction_provider.requests()[1], "<retrieved_example"));
    CHECK_EQ(correction_provider.requests()[1].purpose,
             ai::LlmCallPurpose::RequirementCorrection);
}

void repair_limit_retains_last_candidate_test() {
    const std::string first_invalid = "characterize broken {";
    const std::string second_invalid{invalid_source};
    const std::string third_invalid = "characterize broken {}";
    ai::testing::FakeLlmProvider provider{{proposal(first_invalid),
                                           proposal(second_invalid),
                                           proposal(third_invalid)}};
    ai::AgentSession session{provider, ai::PromptBuilder{}, {2}};
    const auto result = session.generate(
        {ai::NaturalLanguageInstruction{"Create a characterization."}});

    const auto& failure = failed(result);
    CHECK_EQ(session.state(), ai::AgentState::Failed);
    CHECK_EQ(failure.error.code, ai::AgentErrorCode::RepairLimitReached);
    CHECK(failure.proposal.has_value());
    CHECK(!failure.proposal->eligible_to_apply());
    CHECK_EQ(failure.proposal->attempt_count(), std::size_t{3});
    CHECK_EQ(failure.proposal->candidate_source().text(), third_invalid);
    CHECK_EQ(provider.requests().size(), std::size_t{3});
}

void unchanged_invalid_candidate_stops_test() {
    ai::testing::FakeLlmProvider provider{{proposal(invalid_source), proposal(invalid_source)}};
    ai::AgentSession session{provider};
    const auto result = session.generate(
        {ai::NaturalLanguageInstruction{"Create a characterization."}});

    const auto& failure = failed(result);
    CHECK_EQ(failure.error.code, ai::AgentErrorCode::NoProgress);
    CHECK(failure.error.message.find("no progress") != std::string::npos);
    CHECK(failure.proposal.has_value());
    CHECK_EQ(failure.proposal->attempt_count(), std::size_t{2});
    CHECK_EQ(provider.requests().size(), std::size_t{2});
}

void changed_candidate_with_unchanged_diagnostics_stops_test() {
    const std::string changed =
        std::string{invalid_source}.replace(std::string{invalid_source}.find("generated"), 9,
                                            "renamed");
    ai::testing::FakeLlmProvider provider{{proposal(invalid_source), proposal(changed)}};
    ai::AgentSession session{provider};
    const auto result = session.generate(
        {ai::NaturalLanguageInstruction{"Create a characterization."}});

    const auto& failure = failed(result);
    CHECK_EQ(failure.error.code, ai::AgentErrorCode::NoProgress);
    CHECK(failure.error.message.find("diagnostic set did not change") !=
          std::string::npos);
    CHECK_EQ(failure.proposal->candidate_source().text(), changed);
    CHECK_EQ(provider.requests().size(), std::size_t{2});
}

void requirement_violation_is_corrected_test() {
    const std::string wrong = std::string{valid_source};
    const std::string corrected =
        std::string{valid_source}.replace(std::string{valid_source}.find("-10 dBm"), 7,
                                          "-5 dBm");
    ai::testing::FakeLlmProvider provider{{proposal(wrong), proposal(corrected)}};
    ai::AgentSession session{provider};
    const auto result = session.generate(
        {ai::NaturalLanguageInstruction{"Use reference power -5 dBm."}});

    const auto& value = completed(result);
    CHECK_EQ(value.attempt_count(), std::size_t{2});
    CHECK_EQ(value.requirement_verification().status,
             ai::RequirementSatisfactionStatus::Satisfied);
    CHECK_EQ(provider.requests()[1].purpose,
             ai::LlmCallPurpose::RequirementCorrection);
    CHECK(request_contains(provider.requests()[1], "<requirement_violations>"));
    CHECK(request_contains(provider.requests()[1], "reference_power_mismatch"));
}

void explicit_output_name_is_corrected_test() {
    constexpr std::string_view wrong = R"(characterize generated {
  reference power = -25 dBm
  sweep frequency 2.3 GHz .. 2.5 GHz step 100 MHz
  measure actual_power = power
  measure quality = evm
  derive error = actual_power - reference.power
  assert abs(error) <= 1 dB
  assert quality < 3 %
  derive calibration combined_cal {
    correction = -error
    over frequency
  }
}
)";
    constexpr std::string_view corrected = R"(characterize generated {
  reference power = -25 dBm
  sweep frequency 2.3 GHz .. 2.5 GHz step 100 MHz
  measure actual = power
  measure quality = evm
  derive error = actual - reference.power
  assert abs(error) <= 1 dB
  assert quality < 3 %
  derive calibration combined_cal {
    correction = -error
    over frequency
  }
}
)";
    ai::testing::FakeLlmProvider provider{{proposal(wrong), proposal(corrected)}};
    ai::AgentSession session{provider};
    const auto result = session.generate({ai::NaturalLanguageInstruction{
        "Check 2.3 to 2.5 GHz in 100 MHz increments at -25 dBm. "
        "Record output as actual and EVM as quality. The magnitude of actual's "
        "error must not exceed 1 dB, while quality must remain under 3 percent. "
        "Create the frequency calibration."}});

    const auto& value = completed(result);
    CHECK_EQ(value.attempt_count(), std::size_t{2});
    CHECK_EQ(value.requirement_verification().status,
             ai::RequirementSatisfactionStatus::Satisfied);
    CHECK_EQ(provider.requests()[1].purpose,
             ai::LlmCallPurpose::RequirementCorrection);
    CHECK(request_contains(provider.requests()[1],
                           "measurement_missing_or_misnamed"));
}

void unchanged_requirement_violation_stops_test() {
    const std::string changed = std::string{valid_source} + "// still wrong\n";
    ai::testing::FakeLlmProvider provider{{proposal(valid_source), proposal(changed)}};
    ai::AgentSession session{provider};
    const auto result = session.generate(
        {ai::NaturalLanguageInstruction{"Use reference power -5 dBm."}});

    const auto& failure = failed(result);
    CHECK_EQ(failure.error.code, ai::AgentErrorCode::NoProgress);
    CHECK(failure.error.message.find("verified violations did not change") !=
          std::string::npos);
    CHECK_EQ(provider.requests().size(), std::size_t{2});
    CHECK_EQ(provider.requests()[1].purpose,
             ai::LlmCallPurpose::RequirementCorrection);
}

void repair_and_correction_share_budget_test() {
    const std::string corrected =
        std::string{valid_source}.replace(std::string{valid_source}.find("-10 dBm"), 7,
                                          "-5 dBm");
    ai::testing::FakeLlmProvider provider{{proposal(invalid_source),
                                           proposal(valid_source),
                                           proposal(corrected)}};
    ai::AgentSession session{provider, ai::PromptBuilder{}, {1}};
    const auto result = session.generate(
        {ai::NaturalLanguageInstruction{"Use reference power -5 dBm."}});

    const auto& failure = failed(result);
    CHECK_EQ(failure.error.code, ai::AgentErrorCode::RepairLimitReached);
    CHECK(failure.error.message.find("shared") != std::string::npos);
    CHECK(failure.proposal.has_value());
    CHECK(failure.proposal->validation().ok());
    CHECK_EQ(provider.requests().size(), std::size_t{2});
}

void unusable_and_provider_failures_test() {
    ai::testing::FakeLlmProvider malformed{{ai::ProviderResponse{"not json", {"fake", "fixed"}}}};
    ai::AgentSession malformed_session{malformed};
    const auto malformed_result = malformed_session.generate(
        {ai::NaturalLanguageInstruction{"Create a characterization."}});
    CHECK_EQ(failed(malformed_result).error.code, ai::AgentErrorCode::ProviderFailure);
    CHECK(!failed(malformed_result).proposal.has_value());

    ai::testing::FakeLlmProvider unavailable{{
        ai::ProviderError{ai::ProviderErrorCode::Unavailable, "offline"}}};
    ai::AgentSession unavailable_session{unavailable};
    const auto unavailable_result = unavailable_session.generate(
        {ai::NaturalLanguageInstruction{"Create a characterization."}});
    CHECK_EQ(failed(unavailable_result).error.code, ai::AgentErrorCode::ProviderFailure);
    CHECK_EQ(failed(unavailable_result).error.message, "offline");

    ai::testing::FakeLlmProvider repair_failure{{
        proposal(invalid_source),
        ai::ProviderError{ai::ProviderErrorCode::Timeout, "repair timed out"}}};
    ai::AgentSession repair_failure_session{repair_failure};
    const auto repair_failure_result = repair_failure_session.generate(
        {ai::NaturalLanguageInstruction{"Create a characterization."}});
    const auto& repair_failure_value = failed(repair_failure_result);
    CHECK_EQ(repair_failure_value.error.code, ai::AgentErrorCode::ProviderFailure);
    CHECK(repair_failure_value.proposal.has_value());
    CHECK(!repair_failure_value.proposal->eligible_to_apply());
    CHECK_EQ(repair_failure_value.proposal->attempt_count(), std::size_t{1});
}

void cancellation_and_invalid_request_test() {
    ai::testing::FakeLlmProvider provider{{proposal(valid_source)}};
    std::stop_source stop;
    stop.request_stop();
    ai::AgentSession cancelled_session{provider};
    const auto cancelled = cancelled_session.generate(
        {ai::NaturalLanguageInstruction{"Create a characterization."}}, stop.get_token());
    CHECK_EQ(failed(cancelled).error.code, ai::AgentErrorCode::Cancelled);
    CHECK_EQ(cancelled_session.state(), ai::AgentState::Cancelled);
    CHECK(provider.requests().empty());

    ai::AgentSession invalid_session{provider};
    const auto invalid = invalid_session.generate(
        {ai::NaturalLanguageInstruction{" \n"}});
    CHECK_EQ(failed(invalid).error.code, ai::AgentErrorCode::InvalidRequest);
    CHECK_EQ(invalid_session.state(), ai::AgentState::Failed);
    CHECK(provider.requests().empty());

    std::stop_source repair_stop;
    CancelRepairProvider repair_provider{repair_stop};
    ai::AgentSession repair_session{repair_provider};
    const auto repair_cancelled = repair_session.generate(
        {ai::NaturalLanguageInstruction{"Create a characterization."}},
        repair_stop.get_token());
    const auto& repair_failure = failed(repair_cancelled);
    CHECK_EQ(repair_failure.error.code, ai::AgentErrorCode::Cancelled);
    CHECK_EQ(repair_session.state(), ai::AgentState::Cancelled);
    CHECK(repair_failure.proposal.has_value());
    CHECK_EQ(repair_failure.proposal->attempt_count(), std::size_t{1});
    CHECK_EQ(repair_provider.calls(), std::size_t{2});
}

void modification_repair_preserves_context_test() {
    ai::testing::FakeLlmProvider provider{{proposal(invalid_source), proposal(valid_source)}};
    ai::AgentSession session{provider};
    const std::string original{valid_source};
    const std::string instruction{"Change only the reference power to -10 dBm."};
    const auto result = session.modify(
        {ai::ScriptSource{original}, ai::NaturalLanguageInstruction{instruction}});

    CHECK(completed(result).eligible_to_apply());
    CHECK_EQ(provider.requests().size(), std::size_t{2});
    CHECK(request_contains(provider.requests()[1], "<original_horusrf>"));
    CHECK(request_contains(provider.requests()[1], original));
    CHECK(request_contains(provider.requests()[1], instruction));
}

} // namespace

int main() {
    try {
        valid_first_candidate_test();
        unsupported_preflight_skips_provider_test();
        partial_support_is_explicit_on_proposal_test();
        invalid_candidate_is_repaired_test();
        structural_rag_is_initial_generation_only_test();
        repair_limit_retains_last_candidate_test();
        unchanged_invalid_candidate_stops_test();
        changed_candidate_with_unchanged_diagnostics_stops_test();
        requirement_violation_is_corrected_test();
        explicit_output_name_is_corrected_test();
        unchanged_requirement_violation_stops_test();
        repair_and_correction_share_budget_test();
        unusable_and_provider_failures_test();
        cancellation_and_invalid_request_test();
        modification_repair_preserves_context_test();
        std::cout << "All HorusRF AI agent tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
