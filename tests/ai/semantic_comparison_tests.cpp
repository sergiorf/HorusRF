#include "test_support.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "horusrf/ai/golden_dataset.hpp"
#include "horusrf/ai/semantic_comparison.hpp"

namespace {

using namespace horusrf;

constexpr std::string_view baseline = R"(characterize baseline {
  reference power = -10 dBm
  sweep frequency 1 GHz .. 1.1 GHz step 50 MHz
  sweep temperature 273.15 K .. 293.15 K step 10 deltaK
  measure output = power
  measure quality = evm
  derive error = output - reference.power
  assert abs(error) <= 1.5 dB
  assert quality < 2 %
  derive calibration correction_table {
    correction = -error
    over frequency, temperature
  }
}
)";

constexpr std::string_view equivalent = R"(characterize renamed {
  reference power = -10 dBm
  sweep frequency 1000 MHz .. 1100 MHz step 0.05 GHz
  sweep temperature 0 degC .. 20 degC step 10 deltaC
  measure measured_power = power
  measure measured_evm = evm
  derive delta = measured_power - reference.power
  assert measured_evm < 2 %
  assert abs(delta) <= 1.5 dB
  derive calibration another_name {
    correction = -delta
    over frequency, temperature
  }
}
)";

std::string replace_once(std::string source, std::string_view from,
                         std::string_view to) {
    const auto position = source.find(from);
    if (position == std::string::npos) throw std::runtime_error("test input not found");
    source.replace(position, from.size(), to);
    return source;
}

bool has_path(const ai::SemanticComparisonResult& result, std::string_view path) {
    for (const auto& difference : result.differences) {
        if (difference.path == path) return true;
    }
    return false;
}

void require_match(const ai::SemanticComparisonResult& result,
                   std::string_view context) {
    if (result.matches()) return;
    std::string message{context};
    message += " did not match";
    for (const auto& difference : result.differences) {
        message += "\n" + difference.path + ": expected " + difference.expected +
                   ", actual " + difference.actual;
    }
    throw std::runtime_error(message);
}

void identical_and_canonical_equivalence_test() {
    const auto identical = ai::compare_script_semantics(baseline, baseline);
    require_match(identical, "identical script");
    CHECK(identical.differences.empty());

    const auto canonical = ai::compare_script_semantics(baseline, equivalent);
    require_match(canonical, "canonical equivalent script");
    CHECK(canonical.differences.empty());
}

void sweep_and_reference_mismatch_test() {
    auto changed = replace_once(std::string{baseline}, "-10 dBm", "-9 dBm");
    changed = replace_once(std::move(changed), "step 50 MHz", "step 25 MHz");
    const auto result = ai::compare_script_semantics(baseline, changed);
    CHECK_EQ(result.status, ai::SemanticComparisonStatus::Mismatch);
    CHECK(has_path(result, "reference"));
    CHECK(has_path(result, "sweeps[0].step"));
    CHECK(has_path(result, "sweeps[0].point_count"));
}

void missing_sweep_and_calibration_index_test() {
    auto changed = replace_once(
        std::string{baseline},
        "  sweep temperature 273.15 K .. 293.15 K step 10 deltaK\n", "");
    changed = replace_once(std::move(changed),
                           "over frequency, temperature", "over frequency");
    const auto result = ai::compare_script_semantics(baseline, changed);
    CHECK_EQ(result.status, ai::SemanticComparisonStatus::Mismatch);
    CHECK(has_path(result, "sweeps.count"));
    CHECK(has_path(result, "calibration.indexes"));
}

void measurement_and_derived_mismatch_test() {
    auto changed = replace_once(std::string{baseline},
                                "  measure quality = evm\n", "");
    changed = replace_once(std::move(changed), "  assert quality < 2 %\n", "");
    changed = replace_once(std::move(changed),
                           "derive error = output - reference.power",
                           "derive error = output - 0 dBm");
    const auto result = ai::compare_script_semantics(baseline, changed);
    CHECK_EQ(result.status, ai::SemanticComparisonStatus::Mismatch);
    CHECK(has_path(result, "measurements"));
    CHECK(has_path(result, "derived_values"));
    CHECK(has_path(result, "assertions"));
    CHECK(has_path(result, "calibration.correction"));
}

void assertion_mismatch_test() {
    const auto changed = replace_once(std::string{baseline},
                                      "quality < 2 %", "quality <= 3 %");
    const auto result = ai::compare_script_semantics(baseline, changed);
    CHECK_EQ(result.status, ai::SemanticComparisonStatus::Mismatch);
    CHECK(has_path(result, "assertions"));
}

void calibration_mismatch_test() {
    const auto changed = replace_once(std::string{baseline},
                                      "correction = -error", "correction = error");
    const auto result = ai::compare_script_semantics(baseline, changed);
    CHECK_EQ(result.status, ai::SemanticComparisonStatus::Mismatch);
    CHECK(has_path(result, "calibration.correction"));
}

void invalid_source_not_evaluated_test() {
    const auto result = ai::compare_script_semantics(
        baseline, "characterize invalid { measure @ }");
    CHECK_EQ(result.status, ai::SemanticComparisonStatus::NotEvaluated);
    CHECK(has_path(result, "actual.compile"));
}

void checked_in_golden_dataset_test() {
    const auto loaded = ai::load_golden_dataset(
        std::filesystem::path{HORUSRF_SOURCE_DIR} / "tests" / "ai" / "golden");
    CHECK(loaded.ok());
    std::size_t compared{};
    std::size_t unsupported{};
    for (const auto& test_case : loaded.dataset->cases) {
        if (test_case.metadata.comparison == "unsupported") {
            ++unsupported;
            continue;
        }
        const auto result = ai::compare_script_semantics(
            test_case.expected_source, test_case.expected_source);
        if (!result.matches()) {
            throw std::runtime_error("golden case did not match itself: " +
                                     test_case.metadata.id);
        }
        ++compared;
    }
    CHECK_EQ(compared, std::size_t{28});
    CHECK_EQ(unsupported, std::size_t{2});
}

} // namespace

int main() {
    try {
        identical_and_canonical_equivalence_test();
        sweep_and_reference_mismatch_test();
        missing_sweep_and_calibration_index_test();
        measurement_and_derived_mismatch_test();
        assertion_mismatch_test();
        calibration_mismatch_test();
        invalid_source_not_evaluated_test();
        checked_in_golden_dataset_test();
        std::cout << "All HorusRF semantic comparison tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
