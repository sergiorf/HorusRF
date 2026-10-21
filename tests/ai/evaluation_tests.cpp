#include "test_support.hpp"

#include <chrono>
#include <iostream>
#include <sstream>
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
    return {{std::move(id), "test", supported ? "basic" : "unsupported_request",
             "basic", {}, supported, supported ? "semantic" : "unsupported", {}},
            "Create the requested characterization.",
            supported ? valid_source : "// unsupported\n", {}};
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
    CHECK(report.cases[1].unsupported_recognized);
    CHECK_EQ(report.cases[1].semantic_status,
             ai::SemanticComparisonStatus::NotEvaluated);
    CHECK_EQ(report.cases[0].total_latency, std::chrono::milliseconds{100});
    CHECK_EQ(report.summaries.size(), std::size_t{1});
    CHECK_EQ(report.summaries[0].semantic_matches, std::size_t{1});
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
}

} // namespace

int main() {
    try {
        supported_and_unsupported_report_test();
        repair_and_failure_test();
        invalid_settings_test();
        std::cout << "All HorusRF AI evaluation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
