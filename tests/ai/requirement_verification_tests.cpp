#include "test_support.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "horusrf/ai/golden_dataset.hpp"
#include "horusrf/ai/requirement_verification.hpp"

namespace {

using namespace horusrf;

std::string replace_once(std::string source, std::string_view from,
                         std::string_view to) {
    const auto position = source.find(from);
    if (position == std::string::npos) throw std::runtime_error("test input not found");
    source.replace(position, from.size(), to);
    return source;
}

const ai::GoldenCase& find_case(const ai::GoldenDataset& dataset,
                                std::string_view id) {
    for (const auto& item : dataset.cases) {
        if (item.metadata.id == id) return item;
    }
    throw std::runtime_error("golden case not found");
}

bool has_code(const ai::RequirementVerificationResult& result,
              std::string_view code) {
    for (const auto& outcome : result.outcomes) {
        if (outcome.code == code) return true;
    }
    return false;
}

ai::RequirementVerificationResult verify(const ai::GoldenCase& golden,
                                         std::string_view source) {
    const auto facts = ai::compile_program_facts(source);
    if (!facts) throw std::runtime_error("test candidate did not compile to facts");
    return ai::verify_requirements(golden.metadata.requirements, *facts);
}

void projection_and_checked_in_metadata_test(const ai::GoldenDataset& dataset) {
    std::size_t supported{};
    for (const auto& item : dataset.cases) {
        if (!item.metadata.expected_valid) continue;
        ++supported;
        CHECK_EQ(item.metadata.requirement_schema_version, std::size_t{2});
        CHECK(!item.metadata.requirements.empty());
        CHECK(verify(item, item.expected_source).satisfied());
    }
    CHECK_EQ(supported, std::size_t{16});
}

void required_mismatch_tests(const ai::GoldenDataset& dataset) {
    const auto& assertion = find_case(dataset, "frequency_sweep_power_assertion");
    auto source = replace_once(assertion.expected_source,
                               "  assert abs(power_error) <= 1.5 dB\n", "");
    auto result = verify(assertion, source);
    CHECK_EQ(result.status, ai::RequirementSatisfactionStatus::Violated);
    CHECK(has_code(result, "assertion_missing_or_different"));

    source = replace_once(assertion.expected_source, "<= 1.5 dB", "< 1.5 dB");
    result = verify(assertion, source);
    CHECK_EQ(result.status, ai::RequirementSatisfactionStatus::Violated);

    source = replace_once(assertion.expected_source, "1.5 dB", "2 dB");
    result = verify(assertion, source);
    CHECK_EQ(result.status, ai::RequirementSatisfactionStatus::Violated);

    source = replace_once(assertion.expected_source, "step 25 MHz", "step 20 MHz");
    result = verify(assertion, source);
    CHECK(has_code(result, "sweep_mismatch"));

    source = replace_once(assertion.expected_source, "-10 dBm", "-11 dBm");
    result = verify(assertion, source);
    CHECK(has_code(result, "reference_power_mismatch"));

    const auto& named = find_case(dataset, "frequency_sweep_named_power");
    source = replace_once(named.expected_source, "output_power", "measured_power");
    source = replace_once(std::move(source), "output_power", "measured_power");
    result = verify(named, source);
    CHECK(has_code(result, "measurement_missing_or_misnamed"));

    const auto& measurements = find_case(dataset, "frequency_sweep_multiple_measurements");
    source = replace_once(measurements.expected_source,
                          "  measure modulation_evm = evm\n", "");
    result = verify(measurements, source);
    CHECK(has_code(result, "measurement_missing_or_misnamed"));

    const auto& ordered = find_case(dataset, "temperature_first_multidimensional");
    source = replace_once(
        ordered.expected_source,
        "  sweep temperature 10 degC .. 30 degC step 10 deltaC\n"
        "  sweep frequency 2 GHz .. 2.1 GHz step 50 MHz\n",
        "  sweep frequency 2 GHz .. 2.1 GHz step 50 MHz\n"
        "  sweep temperature 10 degC .. 30 degC step 10 deltaC\n");
    source = replace_once(std::move(source), "over temperature, frequency",
                          "over frequency, temperature");
    result = verify(ordered, source);
    CHECK(has_code(result, "sweep_mismatch"));
}

void equivalence_regressions_test(const ai::GoldenDataset& dataset) {
    const auto& units = find_case(dataset, "unit_conversions");
    auto source = replace_once(units.expected_source, "2400000 kHz", "2.4 GHz");
    source = replace_once(std::move(source), "2500000 kHz", "2.5 GHz");
    source = replace_once(std::move(source), "10000 kHz", "10 MHz");
    CHECK(verify(units, source).satisfied());

    const auto& multiple = find_case(dataset, "frequency_sweep_multiple_measurements");
    source = multiple.expected_source;
    source = replace_once(std::move(source), "measure modulation_evm = evm",
                           "measure evm_result = evm");
    source = replace_once(std::move(source),
                           "  derive calibration tx_power {\n    correction = reference.power - output_power",
                           "  derive power_error = output_power - reference.power\n"
                           "  derive calibration tx_power {\n    correction = -power_error");
    CHECK(verify(multiple, source).satisfied());

    const auto& one_point = find_case(dataset, "single_frequency_point");
    source = replace_once(one_point.expected_source, "step 1 MHz", "step 10 MHz");
    CHECK(verify(one_point, source).satisfied());

    const auto& temperature = find_case(dataset, "temperature_sweep");
    source = replace_once(temperature.expected_source, "step 1 MHz", "step 20 MHz");
    CHECK(verify(temperature, source).satisfied());
}

} // namespace

int main() {
    try {
        const auto loaded = ai::load_golden_dataset(
            std::filesystem::path{HORUSRF_SOURCE_DIR} / "tests" / "ai" / "golden");
        if (!loaded.ok()) throw std::runtime_error(loaded.diagnostics.front().message);
        projection_and_checked_in_metadata_test(*loaded.dataset);
        required_mismatch_tests(*loaded.dataset);
        equivalence_regressions_test(*loaded.dataset);
        std::cout << "All HorusRF requirement verification tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
