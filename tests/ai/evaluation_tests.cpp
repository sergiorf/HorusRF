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
        "Create the requested characterization.",
        supported ? valid_source : "// unsupported\n", {}};
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
        response(valid_source, "Power sweeps are unsupported.",
                 {"Cannot represent the requested power sweep."}, metrics),
    }};
    ai::GoldenDataset dataset{"1.0.0", {golden("supported", true),
                                         golden("unsupported", false)}};
    const ai::EvaluationSettings settings{{"test-model"}, 0.0, 512,
                                          std::chrono::seconds{10}, 3, 1,
                                          "2026-10-21T12:00:00Z", "abc123"};
    const auto report = ai::evaluate_golden_dataset(
        dataset, provider, settings, {}, fixed_clock());

    CHECK_EQ(report.cases.size(), std::size_t{2});
    CHECK(report.cases[0].first_attempt_valid);
    CHECK(report.cases[0].final_valid);
    CHECK_EQ(report.cases[0].semantic_status,
             ai::SemanticComparisonStatus::Match);
    CHECK_EQ(report.cases[0].requirement_verification.status,
             ai::RequirementSatisfactionStatus::Satisfied);
    CHECK(report.cases[1].unsupported_recognized);
    CHECK_EQ(report.cases[1].semantic_status,
             ai::SemanticComparisonStatus::NotEvaluated);
    CHECK_EQ(report.cases[0].total_latency, std::chrono::milliseconds{100});
    CHECK_EQ(report.cases[0].warm_cold_status, ai::WarmColdStatus::Cold);
    CHECK_EQ(report.cases[1].warm_cold_status, ai::WarmColdStatus::Warm);
    CHECK(!report.cases[1].final_warnings.empty());
    CHECK_EQ(report.summaries.size(), std::size_t{1});
    CHECK_EQ(report.summaries[0].semantic_matches, std::size_t{1});
    CHECK_EQ(report.summaries[0].requirement_satisfied, std::size_t{1});
    CHECK_EQ(report.summaries[0].unsupported_recognized, std::size_t{1});
    CHECK_NEAR(*report.summaries[0].average_output_tokens_per_second,
               10.0, 1e-9);

    std::ostringstream json;
    ai::write_evaluation_json(json, report);
    CHECK(json.str().find("\"schema\":\"horusrf.ai-evaluation\"") !=
          std::string::npos);
    CHECK(json.str().find("\"unsupported_recognized\":true") !=
          std::string::npos);
    CHECK(json.str().find("\"generated_source\":") != std::string::npos);

    std::ostringstream table;
    ai::write_evaluation_table(table, report);
    CHECK(table.str().find("test-model") != std::string::npos);
    CHECK(table.str().find("Prompt configuration: direct-dsl-v1") !=
          std::string::npos);
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
}

} // namespace

int main() {
    try {
        supported_and_unsupported_report_test();
        repair_and_failure_test();
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
