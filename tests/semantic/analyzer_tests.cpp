#include "test_support.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <variant>

#include "horusrf/parser/parser.hpp"
#include "horusrf/semantic/analyzer.hpp"
#include "regression_corpus.hpp"

namespace {
using namespace horusrf;

std::string fixture() {
    std::ifstream input(std::string{HORUSRF_SOURCE_DIR} +
                        "/examples/tx_path_characterization.hrf", std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

std::string read_source(std::string_view relative_path) {
    std::ifstream input(std::string{HORUSRF_SOURCE_DIR} + "/" +
                        std::string{relative_path}, std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

semantic::AnalysisResult analyze_source(std::string_view source) {
    return semantic::analyze(parser::parse(source));
}

void example_boundary_classification_test() {
    using namespace horusrf::test::parser_regression;
    for (const auto& example : example_cases) {
        const auto result = analyze_source(read_source(example.path));
        CHECK_EQ(result.ok(), example.classification == ExampleClassification::ValidProgram);
    }
}

bool has_code(const semantic::AnalysisResult& result, semantic::DiagnosticCode code) {
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == code) return true;
    }
    return false;
}

semantic::AnalysisResult analyze_expression_failure(std::string_view expression) {
    const auto source =
        std::string{"characterize c { reference power = 0 dBm "
                    "sweep frequency 1 Hz .. 2 Hz step 1 Hz measure power "
                    "derive delta = power - reference.power derive bad = "} +
        std::string{expression} + " }";
    return analyze_source(source);
}

template <typename T>
const T& expression_as(const semantic::Expression& expression) {
    const auto* result = std::get_if<T>(&expression.value);
    CHECK(result != nullptr);
    return *result;
}

void canonical_fixture_test() {
    const auto result = analyze_source(fixture());
    CHECK(result.ok());
    const auto& program = *result.program;
    CHECK_EQ(program.characterization_name, "tx_path");
    CHECK_NEAR(program.reference.value.dbm(), -10.0, 1e-12);
    CHECK_NEAR(program.sweep.start.hertz(), 2.40e9, 1e-3);
    CHECK_NEAR(program.sweep.end.hertz(), 2.50e9, 1e-3);
    CHECK_NEAR(program.sweep.step.hertz(), 1.0e6, 1e-9);
    CHECK(program.reference.symbol != program.sweep.symbol);
    CHECK(program.measurement.symbol != program.sweep.symbol);

    CHECK(program.derived_quantities.empty());

    const auto& calibration = program.calibration;
    CHECK_EQ(calibration.correction.type, semantic::SemanticType::PowerDelta);
    const auto& correction = expression_as<semantic::BinaryValue>(calibration.correction);
    CHECK_EQ(correction.type, semantic::SemanticType::PowerDelta);
    CHECK_EQ(expression_as<semantic::ReferenceValue>(*correction.left).symbol,
             program.reference.symbol);
    CHECK_EQ(expression_as<semantic::SymbolValue>(*correction.right).symbol,
             program.measurement.symbol);
    CHECK_EQ(calibration.indexes.size(), std::size_t{1});
    CHECK_EQ(calibration.indexes.front(), program.sweep.symbol);
    CHECK_EQ(calibration.type.index_types.size(), std::size_t{1});
    CHECK_EQ(calibration.type.index_types.front(), semantic::SemanticType::Frequency);
    CHECK_EQ(calibration.type.correction_type, semantic::SemanticType::PowerDelta);
}

void supported_expression_tests() {
    const auto result = analyze_source(R"(
characterize expressions {
  reference power = -10 dBm
  sweep frequency 1 GHz .. 2 GHz step 1 MHz
  measure power
  derive delta = power - reference.power
  derive shifted = power + delta
  derive combined = delta + 2 dB
  derive inverse = -combined
  derive later = inverse + delta
  derive calibration cal { correction = later over frequency }
})");
    CHECK(result.ok());
    CHECK_EQ(result.program->derived_quantities.size(), std::size_t{5});
    CHECK_EQ(result.program->derived_quantities[0].type, semantic::SemanticType::PowerDelta);
    CHECK_EQ(result.program->derived_quantities[1].type, semantic::SemanticType::Power);
    CHECK_EQ(result.program->derived_quantities[4].type, semantic::SemanticType::PowerDelta);
    const auto& inverse = expression_as<semantic::UnaryValue>(
        result.program->derived_quantities[3].expression);
    CHECK_EQ(inverse.type, semantic::SemanticType::PowerDelta);
}

void assertion_tests() {
    const auto result = analyze_source(R"(
characterize qualified {
  reference power = -10 dBm
  sweep frequency 1 GHz .. 1 GHz step 1 MHz
  measure output_power = power
  measure evm_result = evm
  measure carrier_error = frequency_error
  derive power_error = output_power - reference.power
  assert abs(power_error) <= 1.5 dB
  assert evm_result <= 5 %
  assert abs(carrier_error) <= 20 kHz
  derive calibration cal { correction = -power_error over frequency }
})");
    CHECK(result.ok());
    CHECK_EQ(result.program->assertions.size(), std::size_t{3});
    CHECK_EQ(result.program->assertions[0].id, std::string{"assertion-1"});
    CHECK_EQ(result.program->assertions[2].expression.type, semantic::SemanticType::Boolean);
    const auto& comparison = expression_as<semantic::ComparisonValue>(
        result.program->assertions[2].expression);
    CHECK_EQ(comparison.right->type, semantic::SemanticType::FrequencyDelta);

    auto invalid = analyze_source(R"(
characterize invalid {
 reference power = 0 dBm sweep frequency 1 Hz .. 1 Hz step 1 Hz
 measure power derive error = power - reference.power
 assert error
 derive calibration cal { correction = -error over frequency }
})");
    CHECK(has_code(invalid, semantic::DiagnosticCode::AssertionRequiresBoolean));

    invalid = analyze_source(R"(
characterize invalid {
 reference power = 0 dBm sweep frequency 1 Hz .. 1 Hz step 1 Hz
 measure power assert power <= 1 dB
 derive calibration cal { correction = reference.power - power over frequency }
})");
    CHECK(has_code(invalid, semantic::DiagnosticCode::InvalidComparisonOperands));

    invalid = analyze_source(R"(
characterize invalid {
 reference power = 0 dBm sweep frequency 1 Hz .. 1 Hz step 1 Hz
 measure power assert abs(reference.power) <= 1 dBm
 derive calibration cal { correction = reference.power - power over frequency }
})");
    CHECK(has_code(invalid, semantic::DiagnosticCode::InvalidFunctionArgument));

    invalid = analyze_source(R"(
characterize invalid {
 reference power = 0 dBm sweep frequency 1 Hz .. 1 Hz step 1 Hz
 measure power assert magnitude(1 dB) <= 1 dB
 derive calibration cal { correction = reference.power - power over frequency }
})");
    CHECK(has_code(invalid, semantic::DiagnosticCode::UnknownFunction));
}

void frequency_unit_tests() {
    for (const auto* range : {"1000 Hz .. 2000 Hz step 100 Hz",
                              "1 kHz .. 2 kHz step 0.1 kHz",
                              "0.001 MHz .. 0.002 MHz step 0.0001 MHz",
                              "0.000001 GHz .. 0.000002 GHz step 0.0000001 GHz"}) {
        const auto source =
            std::string{"characterize c { reference power = 0 dBm sweep frequency "} +
            range +
            " measure power derive calibration cal { correction = 0 dB over frequency } }";
        const auto result = analyze_source(source);
        CHECK(result.ok());
        CHECK_NEAR(result.program->sweep.start.hertz(), 1000.0, 1e-8);
    }
}

void typed_sweep_tests() {
    const auto result = analyze_source(R"(
characterize typed_sweeps {
  reference power = -10 dBm
  sweep frequency 2.40 GHz .. 2.50 GHz step 5 MHz
  sweep temperature -20 degC .. 80 degC step 20 deltaC
  measure power
  derive calibration cal {
    correction = reference.power - power
    over frequency, temperature
  }
})");
    CHECK(result.ok());
    CHECK_EQ(result.program->sweeps.size(), std::size_t{2});
    CHECK_EQ(result.program->total_point_count, std::size_t{126});
    const auto& frequency = std::get<semantic::FrequencySweep>(result.program->sweeps[0]);
    const auto& temperature =
        std::get<semantic::TemperatureSweep>(result.program->sweeps[1]);
    CHECK_EQ(frequency.point_count, std::size_t{21});
    CHECK_EQ(temperature.point_count, std::size_t{6});
    CHECK_NEAR(temperature.start.kelvin(), 253.15, 1e-12);
    CHECK_NEAR(temperature.end.kelvin(), 353.15, 1e-12);
    CHECK_NEAR(temperature.step.kelvin(), 20.0, 1e-12);
    CHECK_EQ(result.program->calibration.type.index_types[1],
             semantic::SemanticType::Temperature);

    auto invalid = analyze_source(
        "characterize c { reference power = 0 dBm "
        "sweep frequency 1 Hz .. 2 Hz step 1 Hz "
        "sweep temperature 0 degC .. 20 degC step 10 degC measure power }");
    CHECK(has_code(invalid, semantic::DiagnosticCode::UnexpectedQuantityType));

    invalid = analyze_source(
        "characterize c { reference power = 0 dBm "
        "sweep frequency 1 Hz .. 2 Hz step 1 Hz "
        "sweep temperature -274 degC .. 20 degC step 10 deltaC measure power }");
    CHECK(has_code(invalid, semantic::DiagnosticCode::InvalidNumericLiteral));

    invalid = analyze_source(
        "characterize c { reference power = 0 dBm "
        "sweep frequency 1 Hz .. 1000000 Hz step 1 Hz "
        "sweep temperature 0 K .. 1 K step 1 deltaK measure power }");
    CHECK(has_code(invalid, semantic::DiagnosticCode::SweepTooLarge));

    invalid = analyze_source(
        "characterize c { reference power = 0 dBm "
        "sweep frequency 1 Hz .. 2 Hz step 1 Hz "
        "sweep temperature 0 K .. 1 K step 1 deltaK measure power "
        "derive calibration cal { correction = reference.power - power "
        "over temperature, frequency } }");
    CHECK(has_code(invalid, semantic::DiagnosticCode::InvalidCalibrationDimensions));

    invalid = analyze_source(
        "characterize c { reference power = 0 dBm "
        "sweep frequency 1 Hz .. 2 Hz step 1 Hz "
        "sweep temperature 20 K .. 0 K step 1 deltaK measure power }");
    CHECK(has_code(invalid, semantic::DiagnosticCode::InvalidSweepRange));

    invalid = analyze_source(
        "characterize c { reference power = 0 dBm "
        "sweep frequency 1 Hz .. 2 Hz step 1 Hz "
        "sweep temperature 0 K .. 20 K step -1 deltaK measure power }");
    CHECK(has_code(invalid, semantic::DiagnosticCode::InvalidSweepStep));

    invalid = analyze_source(
        "characterize c { reference power = 0 dBm "
        "sweep frequency 1 Hz .. 2 Hz step 1 Hz "
        "sweep temperature 0 K .. 1 K step 1 deltaK "
        "sweep temperature 2 K .. 3 K step 1 deltaK measure power }");
    CHECK(has_code(invalid, semantic::DiagnosticCode::DuplicateDeclaration));

    invalid = analyze_source(
        "characterize c { sweep frequency -2 Hz .. 0 Hz step 1 Hz "
        "measure power reference power = 0 dBm }");
    CHECK(has_code(invalid, semantic::DiagnosticCode::ReferenceNotAvailable));
}

void rejection_tests() {
    auto result = analyze_source(
        "characterize c { reference power = 1 dB sweep frequency 1 Hz .. 2 Hz step 1 Hz "
        "measure power }");
    CHECK(!result.ok());
    CHECK(!result.program.has_value());
    CHECK(has_code(result, semantic::DiagnosticCode::UnexpectedQuantityType));

    result = analyze_source(
        "characterize c { reference power = 0 dBm sweep frequency 1 dB .. 2 Hz step 0 Hz "
        "measure power }");
    CHECK(has_code(result, semantic::DiagnosticCode::UnexpectedQuantityType));
    CHECK(has_code(result, semantic::DiagnosticCode::InvalidSweepStep));

    result = analyze_source(
        "characterize c { reference power = 0 dBm sweep frequency 1 Hz .. 2 Hz step 1 dB "
        "measure power }");
    CHECK(has_code(result, semantic::DiagnosticCode::UnexpectedQuantityType));

    auto negative_step_program = parser::parse(
        "characterize c { reference power = 0 dBm sweep frequency 1 Hz .. 2 Hz step 1 Hz "
        "measure power }");
    std::get<ast::SweepStatement>(negative_step_program.characterization.statements[1])
        .step.number = "-1";
    result = semantic::analyze(negative_step_program);
    CHECK(has_code(result, semantic::DiagnosticCode::InvalidSweepStep));

    result = analyze_source(
        "characterize c { reference power = 0 dBm sweep frequency 2 Hz .. 1 Hz step 1 Hz "
        "measure power }");
    CHECK(has_code(result, semantic::DiagnosticCode::InvalidSweepRange));

    result = analyze_source(
        "characterize c { reference power = 0 dBm sweep frequency 1 Hz .. 2 Hz step 1 Hz "
        "measure power derive bad = power + power }");
    CHECK(has_code(result, semantic::DiagnosticCode::InvalidBinaryOperands));

    for (const auto* expression : {"delta - power", "power - delta", "delta + power",
                                   "frequency + frequency"}) {
        result = analyze_expression_failure(expression);
        CHECK(has_code(result, semantic::DiagnosticCode::InvalidBinaryOperands));
    }
    result = analyze_expression_failure("-power");
    CHECK(has_code(result, semantic::DiagnosticCode::InvalidUnaryOperand));

    result = analyze_source(
        "characterize c { derive early = power - reference.power reference power = 0 dBm "
        "sweep frequency 1 Hz .. 2 Hz step 1 Hz measure power }");
    CHECK(has_code(result, semantic::DiagnosticCode::ReferenceNotAvailable));

    result = analyze_source(
        "characterize c { reference power = 0 dBm sweep frequency 1 Hz .. 2 Hz step 1 Hz "
        "measure power derive bad = missing }");
    CHECK(has_code(result, semantic::DiagnosticCode::UnknownIdentifier));

    result = analyze_source(
        "characterize c { reference power = 0 dBm sweep frequency 1 Hz .. 2 Hz step 1 Hz "
        "measure power derive bad = reference.missing }");
    CHECK(has_code(result, semantic::DiagnosticCode::UnknownReferenceMember));

    result = analyze_source(
        "characterize c { reference power = 0 dBm sweep frequency 1 Hz .. 2 Hz step 1 Hz "
        "measure power derive first = later derive later = power - reference.power }");
    CHECK(has_code(result, semantic::DiagnosticCode::ReferenceNotAvailable));

    result = analyze_source(
        "characterize c { reference power = 0 dBm reference power = 1 dBm "
        "sweep frequency 1 Hz .. 2 Hz step 1 Hz measure power }");
    CHECK(has_code(result, semantic::DiagnosticCode::DuplicateDeclaration));

    for (const auto* duplicate : {
             "sweep frequency 1 Hz .. 2 Hz step 1 Hz",
             "measure power",
             "derive d = power - reference.power derive d = power - reference.power",
             "derive d = power - reference.power "
             "derive calibration cal { correction = d over frequency } "
             "derive calibration cal { correction = d over frequency }"}) {
        const auto source =
            std::string{"characterize c { reference power = 0 dBm "
                        "sweep frequency 1 Hz .. 2 Hz step 1 Hz measure power "} +
            duplicate + " }";
        result = analyze_source(source);
        CHECK(has_code(result, semantic::DiagnosticCode::DuplicateDeclaration));
    }

    result = analyze_source("characterize c { }");
    CHECK(has_code(result, semantic::DiagnosticCode::MissingReference));
    CHECK(has_code(result, semantic::DiagnosticCode::MissingSweep));
    CHECK(has_code(result, semantic::DiagnosticCode::MissingMeasurement));
    CHECK(has_code(result, semantic::DiagnosticCode::MissingCalibration));

    result = analyze_source(
        "characterize c { reference power = 0 dBm "
        "sweep frequency 1 Hz .. 2 Hz step 1 Hz measure power "
        "derive d = power - reference.power "
        "derive calibration first { correction = d over frequency } "
        "derive calibration second { correction = d over frequency } }");
    CHECK(has_code(result, semantic::DiagnosticCode::DuplicateDeclaration));

    auto program = parser::parse(
        "characterize c { reference power = 0 dBm sweep frequency 1 Hz .. 2 Hz step 1 Hz "
        "measure power derive x = power - reference.power }");
    std::get<ast::DeriveStatement>(program.characterization.statements.back()).name = "power";
    result = semantic::analyze(program);
    CHECK(has_code(result, semantic::DiagnosticCode::DuplicateDeclaration));
}

void calibration_rejection_tests() {
    auto result = analyze_source(
        "characterize c { reference power = 0 dBm sweep frequency 1 Hz .. 2 Hz step 1 Hz "
        "measure power derive calibration cal { correction = power over frequency } }");
    CHECK(has_code(result, semantic::DiagnosticCode::InvalidCalibrationCorrection));

    result = analyze_source(
        "characterize c { reference power = 0 dBm sweep frequency 1 Hz .. 2 Hz step 1 Hz "
        "measure power derive calibration cal { correction = frequency over frequency } }");
    CHECK(has_code(result, semantic::DiagnosticCode::InvalidCalibrationCorrection));

    result = analyze_source(
        "characterize c { reference power = 0 dBm sweep frequency 1 Hz .. 2 Hz step 1 Hz "
        "measure power derive d = power - reference.power "
        "derive calibration cal { correction = d over missing } }");
    CHECK(has_code(result, semantic::DiagnosticCode::UnknownCalibrationDimension));

    result = analyze_source(
        "characterize c { reference power = 0 dBm sweep frequency 1 Hz .. 2 Hz step 1 Hz "
        "measure power derive d = power - reference.power "
        "derive calibration cal { correction = d over power } }");
    CHECK(has_code(result, semantic::DiagnosticCode::NonSweepCalibrationDimension));

    result = analyze_source(
        "characterize c { reference power = 0 dBm sweep frequency 1 Hz .. 2 Hz step 1 Hz "
        "measure power derive d = power - reference.power "
        "derive calibration cal { correction = d over frequency, frequency } }");
    CHECK(has_code(result, semantic::DiagnosticCode::DuplicateCalibrationDimension));
}

void diagnostic_code_name_tests() {
    constexpr std::array mappings{
        std::pair{semantic::DiagnosticCode::InvalidNumericLiteral, "semantic.invalid_numeric_literal"},
        std::pair{semantic::DiagnosticCode::UnexpectedQuantityType, "semantic.unexpected_quantity_type"},
        std::pair{semantic::DiagnosticCode::DuplicateDeclaration, "semantic.duplicate_declaration"},
        std::pair{semantic::DiagnosticCode::MissingReference, "semantic.missing_reference"},
        std::pair{semantic::DiagnosticCode::MissingSweep, "semantic.missing_sweep"},
        std::pair{semantic::DiagnosticCode::MissingMeasurement, "semantic.missing_measurement"},
        std::pair{semantic::DiagnosticCode::UnknownMeasurementOperation, "semantic.unknown_measurement_operation"},
        std::pair{semantic::DiagnosticCode::MissingCalibration, "semantic.missing_calibration"},
        std::pair{semantic::DiagnosticCode::UnknownIdentifier, "semantic.unknown_identifier"},
        std::pair{semantic::DiagnosticCode::UnknownReferenceMember, "semantic.unknown_reference_member"},
        std::pair{semantic::DiagnosticCode::ReferenceNotAvailable, "semantic.reference_not_available"},
        std::pair{semantic::DiagnosticCode::InvalidUnaryOperand, "semantic.invalid_unary_operand"},
        std::pair{semantic::DiagnosticCode::InvalidBinaryOperands, "semantic.invalid_binary_operands"},
        std::pair{semantic::DiagnosticCode::UnknownFunction, "semantic.unknown_function"},
        std::pair{semantic::DiagnosticCode::InvalidFunctionArgument, "semantic.invalid_function_argument"},
        std::pair{semantic::DiagnosticCode::InvalidComparisonOperands, "semantic.invalid_comparison_operands"},
        std::pair{semantic::DiagnosticCode::AssertionRequiresBoolean, "semantic.assertion_requires_boolean"},
        std::pair{semantic::DiagnosticCode::InvalidSweepRange, "semantic.invalid_sweep_range"},
        std::pair{semantic::DiagnosticCode::InvalidSweepStep, "semantic.invalid_sweep_step"},
        std::pair{semantic::DiagnosticCode::SweepTooLarge, "semantic.sweep_too_large"},
        std::pair{semantic::DiagnosticCode::UnknownCalibrationDimension, "semantic.unknown_calibration_dimension"},
        std::pair{semantic::DiagnosticCode::NonSweepCalibrationDimension, "semantic.non_sweep_calibration_dimension"},
        std::pair{semantic::DiagnosticCode::DuplicateCalibrationDimension, "semantic.duplicate_calibration_dimension"},
        std::pair{semantic::DiagnosticCode::InvalidCalibrationDimensions, "semantic.invalid_calibration_dimensions"},
        std::pair{semantic::DiagnosticCode::InvalidCalibrationCorrection, "semantic.invalid_calibration_correction"},
    };
    for (const auto& [code, name] : mappings) {
        CHECK_EQ(semantic::diagnostic_code_name(code), name);
    }
    CHECK_EQ(semantic::diagnostic_code_name(static_cast<semantic::DiagnosticCode>(999)),
             "semantic.unknown");
}

void named_measurement_tests() {
    const auto result = analyze_source(R"(
characterize named {
  reference power = -10 dBm
  sweep frequency 1 GHz .. 2 GHz step 1 GHz
  measure output_power = power
  measure evm_result = evm
  measure carrier_error = frequency_error
  derive power_error = output_power - reference.power
  derive combined_evm = evm_result + 1 %
  derive inverted_error = -carrier_error
  derive calibration cal { correction = -power_error over frequency }
})");
    CHECK(result.ok());
    CHECK_EQ(result.program->measurements.size(), std::size_t{3});
    CHECK_EQ(result.program->measurements[0].type, semantic::SemanticType::Power);
    CHECK_EQ(result.program->measurements[1].type, semantic::SemanticType::Ratio);
    CHECK_EQ(result.program->measurements[2].type, semantic::SemanticType::FrequencyDelta);

    auto rejected = analyze_source(R"(
characterize bad {
 reference power = 0 dBm
 sweep frequency 1 Hz .. 1 Hz step 1 Hz
 measure x = phase
 derive early = later
 measure later = power
 derive calibration cal { correction = reference.power - later over frequency }
})");
    CHECK(has_code(rejected, semantic::DiagnosticCode::UnknownMeasurementOperation));
    CHECK(has_code(rejected, semantic::DiagnosticCode::ReferenceNotAvailable));

    rejected = analyze_source(R"(
characterize duplicate {
 reference power = 0 dBm
 sweep frequency 1 Hz .. 1 Hz step 1 Hz
 measure result = power
 measure result = evm
 derive calibration cal { correction = reference.power - result over frequency }
})");
    CHECK(has_code(rejected, semantic::DiagnosticCode::DuplicateDeclaration));
}
} // namespace

int main() {
    try {
        canonical_fixture_test();
        example_boundary_classification_test();
        supported_expression_tests();
        assertion_tests();
        frequency_unit_tests();
        typed_sweep_tests();
        rejection_tests();
        calibration_rejection_tests();
        named_measurement_tests();
        diagnostic_code_name_tests();
        std::cout << "All HorusRF semantic tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
