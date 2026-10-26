#include "test_support.hpp"

#include <algorithm>
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

bool has_kind(const std::vector<ai::RequirementClause>& clauses,
              ai::RequirementKind kind) {
    return std::ranges::any_of(clauses, [kind](const auto& clause) {
        return clause.kind == kind;
    });
}

bool has_named_measurement(const std::vector<ai::RequirementClause>& clauses,
                           ir::MeasurementOperation operation,
                           std::string_view name) {
    return std::ranges::any_of(clauses, [&](const auto& clause) {
        return clause.kind == ai::RequirementKind::Measurement &&
               clause.measurement &&
               clause.measurement->operation == operation &&
               clause.constrain_measurement_name &&
               clause.measurement->name == name;
    });
}

bool has_assertion_operator(const std::vector<ai::RequirementClause>& clauses,
                            ir::ComparisonOperator op) {
    return std::ranges::any_of(clauses, [op](const auto& clause) {
        return clause.kind == ai::RequirementKind::Assertion &&
               clause.assertion && clause.assertion->op == op;
    });
}

ai::RequirementVerificationResult verify(const ai::GoldenCase& golden,
                                         std::string_view source) {
    const auto facts = ai::compile_program_facts(source);
    if (!facts) throw std::runtime_error("test candidate did not compile to facts");
    return ai::verify_requirements(golden.metadata.requirements, *facts);
}

void projection_and_checked_in_metadata_test(const ai::GoldenDataset& dataset) {
    std::size_t supported{};
    std::size_t baseline_supported{};
    std::size_t expanded_supported{};
    for (const auto& item : dataset.cases) {
        if (!item.metadata.expected_valid) continue;
        ++supported;
        CHECK(item.metadata.requirement_schema_version == 2 ||
              item.metadata.requirement_schema_version == 3);
        baseline_supported += item.metadata.requirement_schema_version == 2 ? 1U : 0U;
        expanded_supported += item.metadata.requirement_schema_version == 3 ? 1U : 0U;
        CHECK(!item.metadata.requirements.empty());
        CHECK(verify(item, item.expected_source).satisfied());
    }
    CHECK_EQ(supported, std::size_t{28});
    CHECK_EQ(baseline_supported, std::size_t{16});
    CHECK_EQ(expanded_supported, std::size_t{12});
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

void interactive_extraction_test(const ai::GoldenDataset& dataset) {
    const auto& units = find_case(dataset, "unit_conversions");
    auto facts = ai::compile_program_facts(units.expected_source);
    CHECK(facts.has_value());
    auto result = ai::verify_instruction_requirements(
        "Use \xe2\x80\x93"
        "10000 mdBm twice: -10000 mdBm. Sweep 2400000 kHz to "
        "2500000 kHz in 10000 kHz steps and measure power.",
        *facts);
    CHECK_EQ(result.status, ai::RequirementSatisfactionStatus::Satisfied);
    const auto extracted_units = ai::extract_requirements(
        "Use -10000 mdBm and sweep 2400000 kHz to 2500000 kHz in 10000 kHz steps.");
    CHECK(!extracted_units.empty());
    CHECK(std::ranges::all_of(extracted_units, [](const auto& clause) {
        return clause.confidence == ai::RequirementConfidence::High &&
               !clause.source_text.empty() && clause.source_end > clause.source_begin;
    }));

    const auto& one_point = find_case(dataset, "single_frequency_point");
    facts = ai::compile_program_facts(one_point.expected_source);
    CHECK(facts.has_value());
    result = ai::verify_instruction_requirements(
        "Measure output power once at 915 MHz with a 3 dBm reference.", *facts);
    CHECK_EQ(result.status, ai::RequirementSatisfactionStatus::Satisfied);
    auto changed = replace_once(one_point.expected_source, "915 MHz .. 915 MHz",
                                "916 MHz .. 916 MHz");
    facts = ai::compile_program_facts(changed);
    CHECK(facts.has_value());
    result = ai::verify_instruction_requirements(
        "Measure output power once at 915 MHz with a 3 dBm reference.", *facts);
    CHECK_EQ(result.status, ai::RequirementSatisfactionStatus::Violated);
    CHECK(!result.outcomes.front().source_text.empty());
    CHECK(result.outcomes.front().source_end > result.outcomes.front().source_begin);

    const auto& basic = find_case(dataset, "frequency_sweep_basic");
    facts = ai::compile_program_facts(basic.expected_source);
    CHECK(facts.has_value());
    result = ai::verify_instruction_requirements(
        "Change only the frequency step to 10 MHz.", *facts);
    CHECK_EQ(result.status, ai::RequirementSatisfactionStatus::Satisfied);

    const auto& named = find_case(dataset, "frequency_sweep_named_power");
    facts = ai::compile_program_facts(named.expected_source);
    CHECK(facts.has_value());
    result = ai::verify_instruction_requirements(
        "Record the power measurement as output_power.", *facts);
    CHECK_EQ(result.status, ai::RequirementSatisfactionStatus::Satisfied);
    changed = replace_once(named.expected_source, "output_power", "other_power");
    changed = replace_once(std::move(changed), "output_power", "other_power");
    facts = ai::compile_program_facts(changed);
    CHECK(facts.has_value());
    result = ai::verify_instruction_requirements(
        "Record the power measurement as output_power.", *facts);
    CHECK_EQ(result.status, ai::RequirementSatisfactionStatus::Violated);

    const auto& combined = find_case(dataset, "combined_limits_paraphrase");
    facts = ai::compile_program_facts(combined.expected_source);
    CHECK(facts.has_value());
    result = ai::verify_instruction_requirements(combined.prompt, *facts);
    CHECK_EQ(result.status, ai::RequirementSatisfactionStatus::Satisfied);
    auto clauses = ai::extract_requirements(combined.prompt);
    CHECK(has_named_measurement(clauses, ir::MeasurementOperation::Power,
                                "actual"));
    CHECK(has_named_measurement(clauses, ir::MeasurementOperation::Evm,
                                "quality"));
    changed = replace_once(combined.expected_source,
                           "measure actual = power",
                           "measure actual_power = power");
    changed = replace_once(std::move(changed),
                           "derive error = actual - reference.power",
                           "derive error = actual_power - reference.power");
    facts = ai::compile_program_facts(changed);
    CHECK(facts.has_value());
    result = ai::verify_instruction_requirements(combined.prompt, *facts);
    CHECK_EQ(result.status, ai::RequirementSatisfactionStatus::Violated);
    CHECK(has_code(result, "measurement_missing_or_misnamed"));

    const auto& chain = find_case(dataset, "derived_measurement_chain");
    clauses = ai::extract_requirements(chain.prompt);
    CHECK(has_named_measurement(clauses, ir::MeasurementOperation::Power,
                                "output"));
    facts = ai::compile_program_facts(chain.expected_source);
    CHECK(facts.has_value());
    CHECK(ai::verify_requirements(clauses, *facts).satisfied());
    changed = replace_once(chain.expected_source,
                           "measure output = power",
                           "measure output_power = power");
    changed = replace_once(std::move(changed),
                           "derive output_error = output - reference.power",
                           "derive output_error = output_power - reference.power");
    facts = ai::compile_program_facts(changed);
    CHECK(facts.has_value());
    result = ai::verify_requirements(clauses, *facts);
    CHECK_EQ(result.status, ai::RequirementSatisfactionStatus::Violated);

    clauses = ai::extract_requirements(
        "Measure power and record output as a CSV file.");
    CHECK(!has_named_measurement(clauses, ir::MeasurementOperation::Power,
                                 "CSV"));
    clauses = ai::extract_requirements(
        "Measure power and write the output as JSON.");
    CHECK(!has_named_measurement(clauses, ir::MeasurementOperation::Power,
                                 "JSON"));

    const auto& assertion = find_case(dataset, "frequency_sweep_power_assertion");
    facts = ai::compile_program_facts(assertion.expected_source);
    CHECK(facts.has_value());
    result = ai::verify_instruction_requirements(
        "Require absolute power error at most 1.5 dB.", *facts);
    CHECK_EQ(result.status, ai::RequirementSatisfactionStatus::Satisfied);
    result = ai::verify_instruction_requirements(
        "Require absolute power error below 1.5 dB.", *facts);
    CHECK_EQ(result.status, ai::RequirementSatisfactionStatus::Violated);

    const auto& strict = find_case(dataset, "strict_power_limit");
    facts = ai::compile_program_facts(strict.expected_source);
    CHECK(facts.has_value());
    result = ai::verify_instruction_requirements(strict.prompt, *facts);
    CHECK_EQ(result.status, ai::RequirementSatisfactionStatus::Satisfied);
    changed = replace_once(strict.expected_source, "< 0.75 dB", "<= 0.75 dB");
    facts = ai::compile_program_facts(changed);
    CHECK(facts.has_value());
    result = ai::verify_instruction_requirements(strict.prompt, *facts);
    CHECK_EQ(result.status, ai::RequirementSatisfactionStatus::Violated);
    CHECK(has_code(result, "assertion_missing_or_different"));

    clauses = ai::extract_requirements(
        "Require absolute power error strictly below 1 dB.");
    CHECK(has_assertion_operator(clauses, ir::ComparisonOperator::Less));
    clauses = ai::extract_requirements(
        "Require absolute power error strictly less than 1 dB.");
    CHECK(has_assertion_operator(clauses, ir::ComparisonOperator::Less));
    clauses = ai::extract_requirements(
        "Require absolute power error strictly above 1 dB.");
    CHECK(has_assertion_operator(clauses, ir::ComparisonOperator::Greater));
    clauses = ai::extract_requirements(
        "Require absolute power error strictly greater than 1 dB.");
    CHECK(has_assertion_operator(clauses, ir::ComparisonOperator::Greater));
    clauses = ai::extract_requirements(
        "Describe whether absolute power error is strictly within 1 dB.");
    CHECK(!has_kind(clauses, ai::RequirementKind::Assertion));
    clauses = ai::extract_requirements(
        "Describe whether absolute power error is strictly at most 1 dB.");
    CHECK(!has_kind(clauses, ai::RequirementKind::Assertion));

    auto wrong_step = replace_once(assertion.expected_source, "step 25 MHz",
                                   "step 20 MHz");
    facts = ai::compile_program_facts(wrong_step);
    CHECK(facts.has_value());
    result = ai::verify_instruction_requirements(
        "Sweep 2.4 GHz to 2.5 GHz every 25 MHz with a -10 dBm reference.",
        *facts);
    CHECK_EQ(result.status, ai::RequirementSatisfactionStatus::Violated);

    const auto& measurements = find_case(dataset, "frequency_sweep_multiple_measurements");
    auto missing_operation = replace_once(measurements.expected_source,
                                          "  measure modulation_evm = evm\n", "");
    facts = ai::compile_program_facts(missing_operation);
    CHECK(facts.has_value());
    result = ai::verify_instruction_requirements("Collect power and EVM.", *facts);
    CHECK_EQ(result.status, ai::RequirementSatisfactionStatus::Violated);

    const auto& ordered = find_case(dataset, "temperature_first_multidimensional");
    auto wrong_order = replace_once(
        ordered.expected_source,
        "  sweep temperature 10 degC .. 30 degC step 10 deltaC\n"
        "  sweep frequency 2 GHz .. 2.1 GHz step 50 MHz\n",
        "  sweep frequency 2 GHz .. 2.1 GHz step 50 MHz\n"
        "  sweep temperature 10 degC .. 30 degC step 10 deltaC\n");
    wrong_order = replace_once(std::move(wrong_order),
                               "over temperature, frequency",
                               "over frequency, temperature");
    facts = ai::compile_program_facts(wrong_order);
    CHECK(facts.has_value());
    result = ai::verify_instruction_requirements(
        "Make temperature the outer sweep from 10 to 30 degC by 10 deltaC, "
        "then sweep frequency from 2 GHz to 2.1 GHz by 50 MHz and calibrate "
        "over temperature and frequency.",
        *facts);
    CHECK_EQ(result.status, ai::RequirementSatisfactionStatus::Violated);

    const auto ambiguous = ai::extract_requirements(
        "Document the power range when it becomes known.");
    CHECK_EQ(ambiguous.size(), std::size_t{1});
    CHECK(ambiguous.front().extraction_code == "ambiguous_power_range");
    CHECK_EQ(ambiguous.front().confidence, ai::RequirementConfidence::Ambiguous);
    CHECK_EQ(ambiguous.front().source_text,
             "Document the power range when it becomes known.");

    const auto none = ai::extract_requirements(
        "Make a useful characterization and retain the existing comments.");
    CHECK(none.empty());
}

void original_suite_false_positive_test(const ai::GoldenDataset& dataset) {
    for (const auto& item : dataset.cases) {
        if (!item.metadata.expected_valid) continue;
        const auto facts = ai::compile_program_facts(item.expected_source);
        CHECK(facts.has_value());
        const auto clauses = ai::extract_requirements(item.prompt);
        CHECK(!clauses.empty());
        CHECK(has_kind(clauses, ai::RequirementKind::ReferencePower) ||
              has_kind(clauses, ai::RequirementKind::Sweep) ||
              has_kind(clauses, ai::RequirementKind::Measurement));
        const auto result = ai::verify_requirements(clauses, *facts);
        if (result.status == ai::RequirementSatisfactionStatus::Violated ||
            result.status == ai::RequirementSatisfactionStatus::Unsupported) {
            throw std::runtime_error("interactive false positive for " +
                                     item.metadata.id);
        }
    }
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
        interactive_extraction_test(*loaded.dataset);
        original_suite_false_positive_test(*loaded.dataset);
        std::cout << "All HorusRF requirement verification tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
