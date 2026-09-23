#include "horusrf/ir/ir.hpp"

#include "test_support.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "horusrf/ir/lowering.hpp"
#include "horusrf/parser/parser.hpp"
#include "horusrf/semantic/analyzer.hpp"
#include "horusrf/semantic/model.hpp"

namespace {
using namespace horusrf;

void diagnostic_code_name_tests() {
    constexpr std::array mappings{
        std::pair{ir::DiagnosticCode::DuplicateSymbol, "lowering.duplicate_symbol"},
        std::pair{ir::DiagnosticCode::UnknownSymbol, "lowering.unknown_symbol"},
        std::pair{ir::DiagnosticCode::TypeMismatch, "lowering.type_mismatch"},
        std::pair{ir::DiagnosticCode::InvalidDefinitionOrder, "lowering.invalid_definition_order"},
        std::pair{ir::DiagnosticCode::InvalidCalibrationIndex, "lowering.invalid_calibration_index"},
        std::pair{ir::DiagnosticCode::UnsupportedExpression, "lowering.unsupported_expression"},
    };
    for (const auto& [code, name] : mappings) CHECK_EQ(ir::diagnostic_code_name(code), name);
    CHECK_EQ(ir::diagnostic_code_name(static_cast<ir::DiagnosticCode>(999)),
             "lowering.unknown");
}

std::string fixture() {
    std::ifstream input(std::string{HORUSRF_SOURCE_DIR} +
                        "/examples/tx_path_characterization.hrf", std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

semantic::AnalyzedProgram analyze_source(std::string_view source) {
    auto analyzed = semantic::analyze(parser::parse(source));
    CHECK(analyzed.ok());
    return std::move(*analyzed.program);
}

ir::Program lower_source(std::string_view source) {
    auto analyzed = analyze_source(source);
    auto lowered = ir::lower(analyzed);
    CHECK(lowered.ok());
    return std::move(*lowered.program);
}

template <typename T>
const T& definition_as(const ir::Value& value) {
    const auto* definition = std::get_if<T>(&value.definition);
    CHECK(definition != nullptr);
    return *definition;
}

bool has_code(const ir::LoweringResult& result, ir::DiagnosticCode code) {
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == code) return true;
    }
    return false;
}

const ir::Value& value_at(const ir::Program& program, ir::ValueId id) {
    CHECK(id.value < program.values.size());
    CHECK_EQ(program.values[id.value].id, id);
    return program.values[id.value];
}

std::vector<ir::ValueId> operands(const ir::Definition& definition) {
    return std::visit(
        [](const auto& operation) -> std::vector<ir::ValueId> {
            using T = std::decay_t<decltype(operation)>;
            if constexpr (std::is_same_v<T, ir::Alias>) {
                return {operation.value};
            } else if constexpr (std::is_same_v<T, ir::NegatePowerDelta>) {
                return {operation.operand};
            } else if constexpr (std::is_same_v<T, ir::AddPowerDelta> ||
                                 std::is_same_v<T, ir::PowerDifference>) {
                return {operation.left, operation.right};
            } else if constexpr (std::is_same_v<T, ir::ApplyPowerDelta>) {
                return {operation.power, operation.delta};
            } else {
                return {};
            }
        },
        definition);
}

void model_test() {
    CHECK(ir::ValueId{0} == ir::ValueId{0});
    CHECK(ir::ValueId{0} != ir::ValueId{1});
    CHECK_EQ(ir::type_name(ir::ValueType::Frequency), "Frequency");
    CHECK_EQ(ir::type_name(ir::ValueType::Power), "Power");
    CHECK_EQ(ir::type_name(ir::ValueType::PowerDelta), "PowerDelta");
    CHECK_EQ(ir::type_name(static_cast<ir::ValueType>(99)), "unknown");

    ir::ConstantValue frequency = domain::Frequency::from_hertz(42.0);
    CHECK_NEAR(std::get<domain::Frequency>(frequency).hertz(), 42.0, 1e-12);
    const ir::ApplyPowerDelta apply{ir::ValueId{3}, ir::ValueId{4}};
    CHECK_EQ(apply.power, ir::ValueId{3});
    CHECK_EQ(apply.delta, ir::ValueId{4});
}

void canonical_pipeline_test() {
    const auto syntax = parser::parse(fixture());
    const auto analyzed = semantic::analyze(syntax);
    CHECK(analyzed.ok());
    const auto result = ir::lower(*analyzed.program);
    CHECK(result.ok());
    const auto& program = *result.program;

    CHECK_EQ(program.characterization_name, "tx_path");
    CHECK_EQ(program.span, analyzed.program->span);
    CHECK_EQ(program.values.size(), std::size_t{5});
    CHECK_EQ(program.reference, ir::ValueId{0});
    CHECK_EQ(program.sweep.frequency, ir::ValueId{1});
    CHECK_EQ(program.measurement, ir::ValueId{2});
    CHECK_NEAR(definition_as<ir::ReferencePower>(value_at(program, program.reference)).value.dbm(),
               -10.0, 1e-12);
    CHECK_NEAR(program.sweep.start.hertz(), 2.40e9, 1e-3);
    CHECK_NEAR(program.sweep.end.hertz(), 2.50e9, 1e-3);
    CHECK_NEAR(program.sweep.step.hertz(), 1.0e6, 1e-9);
    CHECK_EQ(value_at(program, program.sweep.frequency).type, ir::ValueType::Frequency);
    CHECK(std::holds_alternative<ir::SweepFrequency>(
        value_at(program, program.sweep.frequency).definition));
    CHECK_EQ(value_at(program, program.measurement).type, ir::ValueType::Power);

    std::size_t measurements{};
    for (const auto& value : program.values) {
        if (std::holds_alternative<ir::MeasurePower>(value.definition)) ++measurements;
        for (const auto operand : operands(value.definition)) CHECK(operand.value < value.id.value);
    }
    CHECK_EQ(measurements, std::size_t{1});

    CHECK_EQ(program.named_values.size(), std::size_t{1});
    const auto& error = program.named_values.front();
    CHECK_EQ(error.name, "error");
    CHECK_EQ(error.type, ir::ValueType::PowerDelta);
    const auto& error_difference =
        definition_as<ir::PowerDifference>(value_at(program, error.value));
    CHECK_EQ(error_difference.left, program.measurement);
    CHECK_EQ(error_difference.right, program.reference);

    CHECK_EQ(program.calibrations.size(), std::size_t{1});
    const auto& calibration = program.calibrations.front();
    CHECK_EQ(calibration.name, "tx_power");
    CHECK_EQ(calibration.correction_type, ir::ValueType::PowerDelta);
    CHECK_EQ(calibration.indexes.size(), std::size_t{1});
    CHECK_EQ(calibration.indexes.front(), program.sweep.frequency);
    CHECK_EQ(calibration.index_types,
             std::vector<ir::ValueType>{ir::ValueType::Frequency});
    const auto& correction =
        definition_as<ir::PowerDifference>(value_at(program, calibration.correction));
    CHECK_EQ(correction.left, program.reference);
    CHECK_EQ(correction.right, program.measurement);
}

constexpr auto operation_source = R"(
characterize operations {
  reference power = -10 dBm
  sweep frequency 1 GHz .. 2 GHz step 1 MHz
  measure power
  derive literal_frequency = 3 Hz
  derive literal_power = 4 dBm
  derive literal_delta = 2 dB
  derive observed = power
  derive point = frequency
  derive inverse = -literal_delta
  derive shifted = power + literal_delta
  derive combined = literal_delta + inverse
  derive difference = power - reference.power
  derive later = combined + difference
  derive nested = power + (literal_delta + difference)
  derive calibration cal { correction = later over frequency }
})";

void focused_lowering_test() {
    const auto first = lower_source(operation_source);
    const auto second = lower_source(operation_source);
    CHECK_EQ(first, second);
    CHECK_EQ(first.named_values.size(), std::size_t{11});

    CHECK(std::holds_alternative<ir::Constant>(
        value_at(first, first.named_values[0].value).definition));
    CHECK_EQ(first.named_values[0].type, ir::ValueType::Frequency);
    CHECK_EQ(first.named_values[1].type, ir::ValueType::Power);
    CHECK_EQ(first.named_values[2].type, ir::ValueType::PowerDelta);
    CHECK(std::holds_alternative<domain::Frequency>(
        definition_as<ir::Constant>(value_at(first, first.named_values[0].value)).value));
    CHECK(std::holds_alternative<domain::Power>(
        definition_as<ir::Constant>(value_at(first, first.named_values[1].value)).value));
    CHECK(std::holds_alternative<domain::PowerDelta>(
        definition_as<ir::Constant>(value_at(first, first.named_values[2].value)).value));
    CHECK(std::holds_alternative<ir::Alias>(
        value_at(first, first.named_values[3].value).definition));
    CHECK(std::holds_alternative<ir::Alias>(
        value_at(first, first.named_values[4].value).definition));
    CHECK(std::holds_alternative<ir::NegatePowerDelta>(
        value_at(first, first.named_values[5].value).definition));
    CHECK(std::holds_alternative<ir::ApplyPowerDelta>(
        value_at(first, first.named_values[6].value).definition));
    CHECK(std::holds_alternative<ir::AddPowerDelta>(
        value_at(first, first.named_values[7].value).definition));
    CHECK(std::holds_alternative<ir::PowerDifference>(
        value_at(first, first.named_values[8].value).definition));
    CHECK(std::holds_alternative<ir::AddPowerDelta>(
        value_at(first, first.named_values[9].value).definition));
    CHECK(std::holds_alternative<ir::ApplyPowerDelta>(
        value_at(first, first.named_values[10].value).definition));

    const auto& calibration = first.calibrations.front();
    CHECK_EQ(calibration.correction, first.named_values[9].value);
    CHECK_EQ(calibration.indexes.front(), first.sweep.frequency);
    for (const auto& value : first.values) {
        for (const auto operand : operands(value.definition)) CHECK(operand.value < value.id.value);
    }
}

void owning_result_test() {
    const auto result = lower_source(operation_source);
    CHECK_EQ(result.characterization_name, "operations");
    CHECK_EQ(result.named_values.front().name, "literal_frequency");
    CHECK_NEAR(std::get<domain::Frequency>(
                   definition_as<ir::Constant>(
                       value_at(result, result.named_values.front().value)).value).hertz(),
               3.0, 1e-12);
}

void defensive_failure_tests() {
    const auto make_base = [] { return analyze_source(R"(
characterize failures {
  reference power = 0 dBm
  sweep frequency 1 Hz .. 2 Hz step 1 Hz
  measure power
  derive delta = power - reference.power
  derive calibration cal { correction = delta over frequency }
})"); };

    auto broken = make_base();
    auto& difference = std::get<semantic::BinaryValue>(broken.derived_quantities[0].expression.value);
    std::get<semantic::SymbolValue>(difference.left->value).symbol = semantic::SymbolId{999};
    auto result = ir::lower(broken);
    CHECK(!result.ok());
    CHECK(!result.program.has_value());
    CHECK(has_code(result, ir::DiagnosticCode::UnknownSymbol));
    CHECK_EQ(result.diagnostics.front().span, difference.left->span);

    broken = make_base();
    broken.derived_quantities[0].symbol = broken.measurement.symbol;
    result = ir::lower(broken);
    CHECK(has_code(result, ir::DiagnosticCode::DuplicateSymbol));

    broken = make_base();
    broken.derived_quantities[0].type = semantic::SemanticType::Power;
    result = ir::lower(broken);
    CHECK(has_code(result, ir::DiagnosticCode::TypeMismatch));

    broken = make_base();
    broken.derived_quantities[0].expression.type = semantic::SemanticType::Power;
    result = ir::lower(broken);
    CHECK(has_code(result, ir::DiagnosticCode::TypeMismatch));

    broken = make_base();
    auto& invalid_binary =
        std::get<semantic::BinaryValue>(broken.derived_quantities[0].expression.value);
    invalid_binary.op = semantic::BinaryOperator::Add;
    result = ir::lower(broken);
    CHECK(has_code(result, ir::DiagnosticCode::UnsupportedExpression));

    auto unary_program = analyze_source(R"(
characterize unary_failure {
  reference power = 0 dBm
  sweep frequency 1 Hz .. 2 Hz step 1 Hz
  measure power
  derive delta = power - reference.power
  derive inverse = -delta
})");
    auto& unary =
        std::get<semantic::UnaryValue>(unary_program.derived_quantities[1].expression.value);
    unary.operand->type = semantic::SemanticType::Power;
    result = ir::lower(unary_program);
    CHECK(has_code(result, ir::DiagnosticCode::TypeMismatch));

    broken = make_base();
    auto& missing_dependency =
        std::get<semantic::BinaryValue>(broken.derived_quantities[0].expression.value);
    std::get<semantic::SymbolValue>(missing_dependency.left->value).symbol =
        semantic::SymbolId{broken.derived_quantities[0].symbol.value + 1};
    result = ir::lower(broken);
    CHECK(has_code(result, ir::DiagnosticCode::UnknownSymbol));

    broken = make_base();
    broken.calibrations[0].type.correction_type = semantic::SemanticType::Power;
    result = ir::lower(broken);
    CHECK(has_code(result, ir::DiagnosticCode::TypeMismatch));

    broken = make_base();
    broken.calibrations[0].correction = semantic::Expression{
        semantic::SymbolValue{broken.measurement.symbol, "power",
                              semantic::SemanticType::Power,
                              broken.measurement.span},
        semantic::SemanticType::Power,
        broken.measurement.span};
    result = ir::lower(broken);
    CHECK(has_code(result, ir::DiagnosticCode::TypeMismatch));

    broken = make_base();
    broken.calibrations[0].indexes[0] = semantic::SymbolId{999};
    result = ir::lower(broken);
    CHECK(has_code(result, ir::DiagnosticCode::InvalidCalibrationIndex));

    broken = make_base();
    broken.calibrations[0].indexes[0] = broken.measurement.symbol;
    broken.calibrations[0].type.index_types[0] = semantic::SemanticType::Power;
    result = ir::lower(broken);
    CHECK(has_code(result, ir::DiagnosticCode::InvalidCalibrationIndex));

    broken = make_base();
    broken.calibrations[0].indexes.push_back(broken.sweep.symbol);
    broken.calibrations[0].type.index_types.push_back(semantic::SemanticType::Frequency);
    result = ir::lower(broken);
    CHECK(has_code(result, ir::DiagnosticCode::InvalidCalibrationIndex));

    broken = make_base();
    broken.calibrations[0].indexes.clear();
    broken.calibrations[0].type.index_types.clear();
    result = ir::lower(broken);
    CHECK(has_code(result, ir::DiagnosticCode::InvalidCalibrationIndex));

    const auto semantic_failure = semantic::analyze(parser::parse(
        "characterize bad { reference power = 0 dBm measure power }"));
    CHECK(!semantic_failure.ok());
    CHECK(!semantic_failure.program.has_value());
}

} // namespace

int main() {
    try {
        diagnostic_code_name_tests();
        model_test();
        canonical_pipeline_test();
        focused_lowering_test();
        owning_result_test();
        defensive_failure_tests();
        std::cout << "All HorusRF IR tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
