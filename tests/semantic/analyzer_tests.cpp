#include "test_support.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <variant>

#include "horusrf/parser/parser.hpp"
#include "horusrf/semantic/analyzer.hpp"

namespace {
using namespace horusrf;

std::string fixture() {
    std::ifstream input(std::string{HORUSRF_SOURCE_DIR} +
                        "/examples/tx_path_characterization.hrf", std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

semantic::AnalysisResult analyze_source(std::string_view source) {
    return semantic::analyze(parser::parse(source));
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

    CHECK_EQ(program.derived_quantities.size(), std::size_t{1});
    const auto& derived = program.derived_quantities.front();
    CHECK_EQ(derived.type, semantic::SemanticType::PowerDelta);
    const auto& difference = expression_as<semantic::BinaryValue>(derived.expression);
    CHECK_EQ(difference.type, semantic::SemanticType::PowerDelta);
    const auto& measured = expression_as<semantic::SymbolValue>(*difference.left);
    const auto& reference = expression_as<semantic::ReferenceValue>(*difference.right);
    CHECK_EQ(measured.symbol, program.measurement.symbol);
    CHECK_EQ(reference.symbol, program.reference.symbol);
    CHECK_EQ(derived.expression.span.begin.line, std::size_t{12});

    CHECK_EQ(program.calibrations.size(), std::size_t{1});
    const auto& calibration = program.calibrations.front();
    CHECK_EQ(calibration.correction.type, semantic::SemanticType::PowerDelta);
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

void frequency_unit_tests() {
    for (const auto* range : {"1000 Hz .. 2000 Hz step 100 Hz",
                              "1 kHz .. 2 kHz step 0.1 kHz",
                              "0.001 MHz .. 0.002 MHz step 0.0001 MHz",
                              "0.000001 GHz .. 0.000002 GHz step 0.0000001 GHz"}) {
        const auto source = std::string{"characterize c { reference power = 0 dBm sweep frequency "} +
                            range + " measure power }";
        const auto result = analyze_source(source);
        CHECK(result.ok());
        CHECK_NEAR(result.program->sweep.start.hertz(), 1000.0, 1e-8);
    }
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
} // namespace

int main() {
    try {
        canonical_fixture_test();
        supported_expression_tests();
        frequency_unit_tests();
        rejection_tests();
        calibration_rejection_tests();
        std::cout << "All HorusRF semantic tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
