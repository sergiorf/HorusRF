#include "test_support.hpp"

#include <chrono>
#include <iostream>
#include <sstream>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ai/fake_provider.hpp"
#include "horusrf/ai/evaluation.hpp"

namespace {

using namespace horusrf;

const std::string valid_source = R"(characterize generated {
  reference power = 3 dBm
  sweep frequency 915 MHz .. 915 MHz step 1 MHz
  measure power
  derive calibration cal {
    correction = reference.power - power
    over frequency
  }
}
)";

ai::ProviderResponse response(std::string source, std::string explanation = "ok",
                              std::vector<std::string> warnings = {},
                              std::optional<ai::ProviderMetrics> metrics = std::nullopt) {
    std::ostringstream text;
    text << "{\"status\":\"proposal\",\"script\":\"";
    for (const char value : source) {
        if (value == '\n') text << "\\n";
        else if (value == '"') text << "\\\"";
        else if (value == '\\') text << "\\\\";
        else text << value;
    }
    text << "\",\"explanation\":\"" << explanation << "\",\"warnings\":[";
    for (std::size_t index = 0; index < warnings.size(); ++index) {
        if (index != 0) text << ',';
        text << '"' << warnings[index] << '"';
    }
    text << "]}";
    return {text.str(), {"fake", "test-model"}, std::move(metrics)};
}

ai::GoldenCase golden(std::string id, bool supported) {
    ai::GoldenCase result{
        {std::move(id), "test", supported ? "basic" : "unsupported_request",
         "basic", {}, supported, supported ? "semantic" : "unsupported", {}},
        supported
            ? "Create the requested characterization."
            : "At a fixed 2.45 GHz, sweep transmit power from -20 dBm to 0 dBm.",
        supported ? valid_source : "// unsupported\n", {}};
    result.metadata.expected_support = supported
        ? ai::SupportAssessmentStatus::Supported
        : ai::SupportAssessmentStatus::Unsupported;
    if (supported) {
        const auto facts = ai::compile_program_facts(valid_source);
        if (!facts) throw std::runtime_error("test golden source did not compile");
        ai::RequirementClause requirement;
        requirement.id = "reference-power";
        requirement.kind = ai::RequirementKind::ReferencePower;
        requirement.reference_power = facts->reference_power;
        result.metadata.requirement_schema_version = 2;
        result.metadata.requirements.push_back(std::move(requirement));
    }
    return result;
}

ai::EvaluationClock fixed_clock() {
    return [calls = std::size_t{}]() mutable {
        const auto value = std::chrono::steady_clock::time_point{} +
                           std::chrono::milliseconds{100 * calls};
        ++calls;
        return value;
    };
}

void supported_and_unsupported_report_test() {
    ai::ProviderMetrics metrics;
    metrics.generation_duration = std::chrono::seconds{2};
    metrics.output_token_count = 20;
    ai::testing::FakeLlmProvider provider{{
        response(valid_source, "ok", {}, metrics),
    }};
    ai::GoldenDataset dataset{"1.0.0", {golden("supported", true),
                                         golden("unsupported", false)}};
    const ai::EvaluationSettings settings{{"test-model"}, 0.0, 512,
                                          std::chrono::seconds{10}, 3, 1,
                                          "2026-10-21T12:00:00Z", "abc123"};
    struct ProgressEvent final {
        ai::EvaluationProgressStage stage;
        std::string case_id;
        std::size_t completed;
        std::size_t total;
    };
    std::vector<ProgressEvent> progress_events;
    const auto report = ai::evaluate_golden_dataset(
        dataset, provider, settings, {}, fixed_clock(),
        [&](ai::EvaluationProgressStage stage, const ai::CaseEvaluation& item,
            std::size_t completed, std::size_t total) {
            progress_events.push_back({stage, item.case_id, completed, total});
        });

    CHECK_EQ(report.cases.size(), std::size_t{2});
    CHECK(report.cases[0].first_attempt_valid);
    CHECK(report.cases[0].final_valid);
    CHECK_EQ(report.cases[0].semantic_status,
             ai::SemanticComparisonStatus::Match);
    CHECK_EQ(report.cases[0].requirement_verification.status,
             ai::RequirementSatisfactionStatus::Satisfied);
    CHECK(report.cases[1].unsupported_recognized);
    CHECK(report.cases[0].support_assessment_matches);
    CHECK(report.cases[1].support_assessment_matches);
    CHECK_EQ(report.cases[1].generation_status,
             ai::EvaluationGenerationStatus::Unsupported);
    CHECK(report.cases[1].attempts.empty());
    CHECK_EQ(report.cases[1].support_assessment.status,
             ai::SupportAssessmentStatus::Unsupported);
    CHECK_EQ(report.cases[1].semantic_status,
             ai::SemanticComparisonStatus::NotEvaluated);
    CHECK_EQ(report.cases[0].total_latency, std::chrono::milliseconds{100});
    CHECK_EQ(report.cases[0].attempts[0].compiler_duration,
             std::optional{std::chrono::milliseconds{100}});
    CHECK_EQ(report.cases[0].attempts[0].verification_duration,
             std::optional{std::chrono::milliseconds{100}});
    CHECK_EQ(report.cases[0].warm_cold_status, ai::WarmColdStatus::Cold);
    CHECK_EQ(report.cases[1].warm_cold_status, ai::WarmColdStatus::Warm);
    CHECK(report.cases[1].final_warnings.empty());
    CHECK_EQ(report.summaries.size(), std::size_t{1});
    CHECK_EQ(report.summaries[0].semantic_matches, std::size_t{1});
    CHECK_EQ(report.summaries[0].requirement_satisfied, std::size_t{1});
    CHECK_EQ(report.summaries[0].unsupported_recognized, std::size_t{1});
    CHECK_EQ(report.summaries[0].support_assessment_matches, std::size_t{2});
    CHECK_EQ(report.summaries[0].distinct_cases, std::size_t{2});
    CHECK_EQ(report.summaries[0].repetition_count, std::size_t{1});
    CHECK_EQ(report.case_trends.size(), std::size_t{2});
    CHECK_EQ(report.summaries[0].cold_latency.sample_count, std::size_t{1});
    CHECK_EQ(report.summaries[0].cold_latency.median,
             std::chrono::milliseconds{100});
    CHECK_EQ(report.summaries[0].warm_latency.sample_count, std::size_t{1});
    CHECK_EQ(report.summaries[0].warm_latency.p90,
             std::chrono::milliseconds{100});
    CHECK_NEAR(*report.summaries[0].average_output_tokens_per_second,
               10.0, 1e-9);
    CHECK_EQ(progress_events.size(), std::size_t{4});
    CHECK_EQ(progress_events[0].stage, ai::EvaluationProgressStage::Started);
    CHECK_EQ(progress_events[0].case_id, std::string{"supported"});
    CHECK_EQ(progress_events[0].completed, std::size_t{0});
    CHECK_EQ(progress_events[0].total, std::size_t{2});
    CHECK_EQ(progress_events[1].stage, ai::EvaluationProgressStage::Completed);
    CHECK_EQ(progress_events[1].completed, std::size_t{1});
    CHECK_EQ(progress_events[3].case_id, std::string{"unsupported"});
    CHECK_EQ(progress_events[3].completed, std::size_t{2});

    std::ostringstream json;
    ai::write_evaluation_json(json, report);
    CHECK(json.str().find("\"schema\":\"horusrf.ai-evaluation\"") !=
          std::string::npos);
    CHECK(json.str().find("\"unsupported_recognized\":true") !=
          std::string::npos);
    CHECK(json.str().find("\"scoring_version\":\"evaluation-trends-v1\"") !=
          std::string::npos);
    CHECK(json.str().find("\"case_trends\":[{") != std::string::npos);
    CHECK(json.str().find("\"support_assessment\":{\"status\":\"unsupported\"") !=
          std::string::npos);
    CHECK(json.str().find("\"generated_source\":") != std::string::npos);
    CHECK(json.str().find("\"compiler_duration_ns\":100000000") !=
          std::string::npos);
    CHECK(json.str().find("\"verification_duration_ns\":100000000") !=
          std::string::npos);
    CHECK(json.str().find(
              "\"cold_latency\":{\"sample_count\":1,\"median_ms\":100,\"p90_ms\":100}") !=
          std::string::npos);
    CHECK(json.str().find(
              "\"warm_latency\":{\"sample_count\":1,\"median_ms\":100,\"p90_ms\":100}") !=
          std::string::npos);

    std::ostringstream table;
    ai::write_evaluation_table(table, report);
    CHECK(table.str().find("test-model") != std::string::npos);
    CHECK(table.str().find("Prompt configuration: direct-dsl-v1") !=
          std::string::npos);
}

void repetition_trend_test() {
    ai::testing::FakeLlmProvider provider{{
        response(valid_source),
        ai::ProviderError{ai::ProviderErrorCode::Unavailable, "offline"},
        response(valid_source),
    }};
    ai::GoldenDataset dataset{"2.0.0", {golden("trend", true)}};
    ai::EvaluationSettings settings{{"test-model"}, 0.0, 512,
                                    std::chrono::seconds{10}, 3, 3, {}, {}};
    const auto report = ai::evaluate_golden_dataset(
        dataset, provider, settings, {}, fixed_clock());

    CHECK_EQ(report.case_trends.size(), std::size_t{1});
    const auto& trend = report.case_trends.front();
    CHECK_EQ(trend.repetitions, std::size_t{3});
    CHECK_EQ(trend.requirement_satisfied, std::size_t{2});
    CHECK_EQ(trend.final_valid, std::size_t{2});
    CHECK(!trend.persistent_requirement_failure);
    CHECK_EQ(trend.latency.sample_count, std::size_t{3});
    CHECK_EQ(report.summaries[0].repetition_count, std::size_t{3});
    CHECK_NEAR(report.summaries[0].median_case_requirement_success_rate,
               2.0 / 3.0, 1e-9);
    CHECK(report.summaries[0].repetition_success_rate_standard_deviation > 0.0);

    std::ostringstream json;
    ai::write_evaluation_json(json, report);
    CHECK(json.str().find("\"repetitions\":3") != std::string::npos);
    CHECK(json.str().find("\"persistent_requirement_failure\":false") !=
          std::string::npos);
    std::ostringstream table;
    ai::write_evaluation_table(table, report);
    CHECK(table.str().find("Per-case persistence") != std::string::npos);
}

void typed_support_denominator_test() {
    auto partial = golden("partial", true);
    partial.prompt = "Sweep frequency and also sweep transmit power from -20 dBm "
                     "to 0 dBm.";
    partial.metadata.expected_support = ai::SupportAssessmentStatus::PartiallySupported;
    auto uncertain = golden("uncertain", false);
    uncertain.prompt = "Characterize a power range suitable for a receiver.";
    uncertain.metadata.expected_support = ai::SupportAssessmentStatus::Uncertain;
    ai::testing::FakeLlmProvider provider{{response(valid_source),
                                           response(valid_source)}};
    ai::GoldenDataset dataset{"2.0.0", {std::move(partial), std::move(uncertain)}};
    const ai::EvaluationSettings settings{{"test-model"}, 0.0, 512,
                                          std::chrono::seconds{10}, 3, 1, {}, {}};
    const auto report = ai::evaluate_golden_dataset(
        dataset, provider, settings, {}, fixed_clock());
    const auto& summary = report.summaries.front();
    CHECK_EQ(summary.total_cases, std::size_t{2});
    CHECK_EQ(summary.applicable_cases, std::size_t{1});
    CHECK_EQ(summary.supported_cases, std::size_t{0});
    CHECK_EQ(summary.partially_supported_cases, std::size_t{1});
    CHECK_EQ(summary.unsupported_cases, std::size_t{0});
    CHECK_EQ(summary.uncertain_cases, std::size_t{1});
    CHECK_EQ(summary.support_assessment_matches, std::size_t{2});
    CHECK_EQ(summary.unsupported_recognized, std::size_t{0});
}

void repair_and_failure_test() {
    ai::testing::FakeLlmProvider repair_provider{{
        response("characterize broken {}"), response(valid_source),
    }};
    ai::GoldenDataset dataset{"1.0.0", {golden("repair", true)}};
    const ai::EvaluationSettings settings{{"test-model"}, 0.0, 512,
                                          std::chrono::seconds{10}, 3, 1, {}, {}};
    const auto repaired = ai::evaluate_golden_dataset(
        dataset, repair_provider, settings, {}, fixed_clock());
    CHECK(!repaired.cases[0].first_attempt_valid);
    CHECK(repaired.cases[0].final_valid);
    CHECK_EQ(repaired.cases[0].repair_attempts, std::size_t{1});
    CHECK_EQ(repaired.cases[0].provider_attempts.size(), std::size_t{2});
    CHECK_EQ(repaired.cases[0].attempts.size(), std::size_t{2});
    CHECK_EQ(repaired.cases[0].attempts[0].purpose,
             ai::EvaluationCallPurpose::Generation);
    CHECK_EQ(repaired.cases[0].attempts[1].purpose,
             ai::EvaluationCallPurpose::CompilerRepair);
    CHECK(repaired.cases[0].attempts[0].candidate_hash.has_value());
    CHECK(repaired.cases[0].attempts[1].candidate_hash.has_value());
    CHECK(!repaired.cases[0].attempts[1].repeated_candidate);
    CHECK_EQ(repaired.cases[0].attempts[1].diagnostic_set_changed,
             std::optional<bool>{true});
    CHECK_EQ(repaired.cases[0].attempts[0].structured_response_status,
             ai::StructuredResponseStatus::Parsed);
    CHECK_EQ(repaired.cases[0].attempts[0].compiler_valid,
             std::optional<bool>{false});
    CHECK(!repaired.cases[0].attempts[0].compiler_diagnostics.empty());
    CHECK_EQ(repaired.cases[0].attempts[1].compiler_valid,
             std::optional<bool>{true});
    CHECK(repaired.cases[0].attempts[0].prompt_size.total_utf8_bytes > 0);
    CHECK(!repaired.cases[0].attempts[0].prompt_size.sections.empty());
    CHECK(!repaired.cases[0].attempts[1].provider_metrics.has_value());

    ai::testing::FakeLlmProvider failed{{
        ai::ProviderError{ai::ProviderErrorCode::Unavailable, "offline"},
    }};
    const auto failure = ai::evaluate_golden_dataset(
        dataset, failed, settings, {}, fixed_clock());
    CHECK_EQ(failure.cases[0].generation_status,
             ai::EvaluationGenerationStatus::Failed);
    CHECK(failure.cases[0].error.has_value());
    CHECK_EQ(failure.cases[0].error->code, ai::AgentErrorCode::ProviderFailure);
    CHECK(!failure.cases[0].generated_source.has_value());
    CHECK_EQ(failure.cases[0].attempts.size(), std::size_t{1});
    CHECK_EQ(failure.cases[0].attempts[0].structured_response_status,
             ai::StructuredResponseStatus::ProviderError);
    CHECK_EQ(failure.cases[0].attempts[0].provider_error_code,
             std::optional{ai::ProviderErrorCode::Unavailable});

    ai::testing::FakeLlmProvider output_limited{{
        ai::ProviderError{ai::ProviderErrorCode::OutputLimit,
                          "provider output limit reached"},
    }};
    const auto output_limit_report = ai::evaluate_golden_dataset(
        dataset, output_limited, settings, {}, fixed_clock());
    CHECK_EQ(output_limit_report.cases[0].attempts[0].provider_error_code,
             std::optional{ai::ProviderErrorCode::OutputLimit});
    CHECK_EQ(output_limit_report.summaries[0].output_limit_errors,
             std::size_t{1});
    std::ostringstream output_limit_json;
    ai::write_evaluation_json(output_limit_json, output_limit_report);
    CHECK(output_limit_json.str().find("\"provider_error_code\":\"output_limit\"") !=
          std::string::npos);
    CHECK(output_limit_json.str().find("\"output_limit_errors\":1") !=
          std::string::npos);
}

void requirement_correction_is_observable_test() {
    std::string wrong_source = valid_source;
    wrong_source.replace(wrong_source.find("3 dBm"), 5, "4 dBm");
    auto correction_case = golden("correction", true);
    correction_case.prompt = "Use reference power 3 dBm.";
    ai::testing::FakeLlmProvider provider{{response(wrong_source),
                                           response(valid_source)}};
    const ai::GoldenDataset dataset{"1.0.0", {std::move(correction_case)}};
    const ai::EvaluationSettings settings{{"test-model"}, 0.0, 512,
                                          std::chrono::seconds{10}, 3, 1, {}, {}};
    const auto report = ai::evaluate_golden_dataset(
        dataset, provider, settings, {}, fixed_clock());

    CHECK_EQ(report.cases[0].generation_status,
             ai::EvaluationGenerationStatus::Completed);
    CHECK_EQ(report.cases[0].attempts.size(), std::size_t{2});
    CHECK_EQ(report.cases[0].attempts[1].purpose,
             ai::EvaluationCallPurpose::RequirementCorrection);
    CHECK_EQ(report.cases[0].requirement_verification.status,
             ai::RequirementSatisfactionStatus::Satisfied);

    std::ostringstream json;
    ai::write_evaluation_json(json, report);
    CHECK(json.str().find("\"purpose\":\"requirement_correction\"") !=
          std::string::npos);
    CHECK(json.str().find("\"candidate_hash\":") != std::string::npos);
    CHECK(json.str().find("\"diagnostic_set_changed\":") !=
          std::string::npos);
}

void schema_v2_diagnostics_and_differences_test() {
    const std::string different_source = R"(characterize generated {
  reference power = 4 dBm
  sweep frequency 915 MHz .. 915 MHz step 1 MHz
  measure power
  derive calibration cal {
    correction = reference.power - power
    over frequency
  }
}
)";
    ai::ProviderMetrics metrics;
    metrics.input_token_count = 123;
    ai::testing::FakeLlmProvider provider{{
        response("characterize broken {}", "invalid", {"first warning"}),
        response(different_source, "repaired", {}, metrics),
    }};
    ai::GoldenDataset dataset{"1.0.0", {golden("diagnostic", true)}};
    const ai::EvaluationSettings settings{{"test-model"}, 0.0, 512,
                                          std::chrono::seconds{10}, 3, 1,
                                          "2026-10-21T12:00:00Z", "abc123"};
    const auto report = ai::evaluate_golden_dataset(
        dataset, provider, settings, {}, fixed_clock());

    CHECK_EQ(report.prompt_configuration_id, "direct-dsl-v1");
    CHECK_EQ(report.comparison_version, "semantic-ir-v2");
    CHECK_EQ(report.cases[0].run_order, std::size_t{1});
    CHECK_EQ(report.cases[0].warm_cold_status, ai::WarmColdStatus::Cold);
    CHECK_EQ(report.cases[0].attempts[1].semantic_status,
             ai::SemanticComparisonStatus::Mismatch);
    CHECK_EQ(report.cases[0].attempts[1].requirement_verification.status,
             ai::RequirementSatisfactionStatus::Violated);
    CHECK(!report.cases[0].attempts[1].semantic_differences.empty());
    CHECK_EQ(report.cases[0].attempts[1].semantic_differences[0].path,
             "reference");

    std::ostringstream json;
    ai::write_evaluation_json(json, report);
    CHECK(json.str().find("\"version\":2") != std::string::npos);
    CHECK(json.str().find("\"prompt_configuration_id\":\"direct-dsl-v1\"") !=
          std::string::npos);
    CHECK(json.str().find("\"purpose\":\"compiler_repair\"") !=
          std::string::npos);
    CHECK(json.str().find("\"compiler_diagnostics\":[{") !=
          std::string::npos);
    CHECK(json.str().find("\"semantic_differences\":[{") !=
          std::string::npos);
    CHECK(json.str().find("\"requirement_schema_version\":\"golden-requirements-v2\"") !=
          std::string::npos);
    CHECK(json.str().find("\"requirement_verification\":{\"status\":\"violated\"") !=
          std::string::npos);
    CHECK(json.str().find("\"input_token_count\":123") !=
          std::string::npos);
    CHECK(json.str().find("\"model_digest\":null") != std::string::npos);
}

void malformed_response_and_cancellation_test() {
    ai::GoldenDataset dataset{"1.0.0", {golden("edge", true)}};
    const ai::EvaluationSettings settings{{"test-model"}, 0.0, 512,
                                          std::chrono::seconds{10}, 3, 1, {}, {}};
    ai::testing::FakeLlmProvider malformed{{
        ai::ProviderResponse{"not json", {"fake", "test-model"}, std::nullopt},
    }};
    const auto malformed_report = ai::evaluate_golden_dataset(
        dataset, malformed, settings, {}, fixed_clock());
    CHECK_EQ(malformed_report.cases[0].attempts.size(), std::size_t{1});
    CHECK_EQ(malformed_report.cases[0].attempts[0].structured_response_status,
             ai::StructuredResponseStatus::MalformedJson);
    CHECK(malformed_report.cases[0].attempts[0].response_error.has_value());

    ai::testing::FakeLlmProvider schema_violation{{
        ai::ProviderResponse{"{}", {"fake", "test-model"}, std::nullopt},
    }};
    const auto schema_report = ai::evaluate_golden_dataset(
        dataset, schema_violation, settings, {}, fixed_clock());
    CHECK_EQ(schema_report.cases[0].attempts[0].structured_response_status,
             ai::StructuredResponseStatus::SchemaViolation);

    ai::testing::FakeLlmProvider cancelled_provider{{response(valid_source)}};
    std::stop_source cancellation;
    cancellation.request_stop();
    const auto cancelled = ai::evaluate_golden_dataset(
        dataset, cancelled_provider, settings, cancellation.get_token(), fixed_clock());
    CHECK_EQ(cancelled.cases[0].generation_status,
             ai::EvaluationGenerationStatus::Cancelled);
    CHECK(cancelled.cases[0].attempts.empty());
    CHECK(cancelled_provider.requests().empty());
}

void invalid_settings_test() {
    ai::testing::FakeLlmProvider provider{{}};
    ai::GoldenDataset dataset{"1.0.0", {}};
    bool threw = false;
    try {
        static_cast<void>(ai::evaluate_golden_dataset(
            dataset, provider, ai::EvaluationSettings{}));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    ai::EvaluationSettings unknown{{"test-model"}};
    unknown.prompt_configuration_id = "not-a-prompt";
    ai::GoldenDataset nonempty{"1.0.0", {golden("prompt", true)}};
    threw = false;
    try {
        static_cast<void>(ai::evaluate_golden_dataset(nonempty, provider, unknown));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    unknown.prompt_configuration_id = "structural-rag-v1";
    threw = false;
    try {
        static_cast<void>(ai::evaluate_golden_dataset(nonempty, provider, unknown));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

} // namespace

int main() {
    try {
        supported_and_unsupported_report_test();
        repetition_trend_test();
        typed_support_denominator_test();
        repair_and_failure_test();
        requirement_correction_is_observable_test();
        schema_v2_diagnostics_and_differences_test();
        malformed_response_and_cancellation_test();
        invalid_settings_test();
        std::cout << "All HorusRF AI evaluation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
