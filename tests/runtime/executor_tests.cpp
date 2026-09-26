#include "horusrf/runtime/executor.hpp"

#include "test_support.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "horusrf/device/simulated_rf_device.hpp"
#include "horusrf/ir/lowering.hpp"
#include "horusrf/parser/parser.hpp"
#include "horusrf/semantic/analyzer.hpp"

namespace {
using namespace horusrf;

void diagnostic_code_name_tests() {
    constexpr std::array mappings{
        std::pair{runtime::DiagnosticCode::InvalidValueId, "runtime.invalid_value_id"},
        std::pair{runtime::DiagnosticCode::InvalidDefinitionOrder, "runtime.invalid_definition_order"},
        std::pair{runtime::DiagnosticCode::TypeMismatch, "runtime.type_mismatch"},
        std::pair{runtime::DiagnosticCode::InvalidProgramBinding, "runtime.invalid_program_binding"},
        std::pair{runtime::DiagnosticCode::InvalidOperation, "runtime.invalid_operation"},
        std::pair{runtime::DiagnosticCode::InvalidSweep, "runtime.invalid_sweep"},
        std::pair{runtime::DiagnosticCode::SweepTooLarge, "runtime.sweep_too_large"},
        std::pair{runtime::DiagnosticCode::InvalidCalibrationBinding, "runtime.invalid_calibration_binding"},
    };
    for (const auto& [code, name] : mappings) {
        CHECK_EQ(runtime::diagnostic_code_name(code), name);
    }
    CHECK_EQ(runtime::diagnostic_code_name(static_cast<runtime::DiagnosticCode>(999)),
             "runtime.unknown");
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

ir::Program lower_source(std::string_view source) {
    const auto syntax = parser::parse(source);
    auto analyzed = semantic::analyze(syntax);
    CHECK(analyzed.ok());
    auto lowered = ir::lower(*analyzed.program);
    CHECK(lowered.ok());
    return std::move(*lowered.program);
}

struct RecordingTester : device::RfDevice {
    void setFrequency(domain::Frequency value) override {
        calls.push_back("frequency");
        frequency = value;
    }
    void setOutputPower(domain::Power value) override {
        calls.push_back("power");
        power = value;
    }
    std::vector<std::string> calls;
    domain::Frequency frequency = domain::Frequency::from_hertz(0.0);
    domain::Power power = domain::Power::from_dbm(0.0);
};

struct RecordingMeasurement : device::MeasurementDevice {
    explicit RecordingMeasurement(RecordingTester& tester) : tester_(tester) {}

    domain::Power measurePower() override {
        tester_.calls.push_back("measure");
        return measured;
    }

    RecordingTester& tester_;
    domain::Power measured = domain::Power::from_dbm(-9.25);
};

const runtime::RuntimeValue& named(const ir::Program& program,
                                   const runtime::PointEvaluation& evaluation,
                                   std::string_view name) {
    for (const auto& value : program.named_values) {
        if (value.name == name) return evaluation.at(value.value);
    }
    throw std::runtime_error("missing named value");
}

void operation_matrix_and_device_order_test() {
    const auto program = lower_source(operation_source);
    RecordingTester tester;
    RecordingMeasurement measurement{tester};
    const auto point = domain::Frequency::from_hertz(1.5e9);
    const auto result = runtime::execute_point(program, point, tester, measurement);
    CHECK(result.ok());
    CHECK_EQ(result.evaluation->values.size(), program.values.size());
    CHECK_EQ(tester.calls, std::vector<std::string>({"frequency", "power", "measure"}));
    CHECK_EQ(tester.frequency, point);
    CHECK_NEAR(tester.power.dbm(), -10.0, 1e-12);
    CHECK_EQ(result.evaluation->frequency, point);
    CHECK_NEAR(std::get<domain::Frequency>(named(program, *result.evaluation,
                                                "literal_frequency")).hertz(),
               3.0, 1e-12);
    CHECK_NEAR(std::get<domain::Power>(named(program, *result.evaluation,
                                            "literal_power")).dbm(),
               4.0, 1e-12);
    CHECK_NEAR(std::get<domain::PowerDelta>(named(program, *result.evaluation,
                                                 "literal_delta")).db(),
               2.0, 1e-12);
    CHECK_NEAR(std::get<domain::Power>(named(program, *result.evaluation, "observed")).dbm(),
               -9.25, 1e-12);
    CHECK_NEAR(std::get<domain::Frequency>(named(program, *result.evaluation, "point")).hertz(),
               1.5e9, 1e-3);
    CHECK_NEAR(std::get<domain::PowerDelta>(named(program, *result.evaluation, "inverse")).db(),
               -2.0, 1e-12);
    CHECK_NEAR(std::get<domain::Power>(named(program, *result.evaluation, "shifted")).dbm(),
               -7.25, 1e-12);
    CHECK_NEAR(std::get<domain::PowerDelta>(named(program, *result.evaluation, "combined")).db(),
               0.0, 1e-12);
    CHECK_NEAR(std::get<domain::PowerDelta>(named(program, *result.evaluation, "difference")).db(),
               0.75, 1e-12);
    CHECK_NEAR(std::get<domain::PowerDelta>(named(program, *result.evaluation, "later")).db(),
               0.75, 1e-12);
    CHECK_NEAR(std::get<domain::Power>(named(program, *result.evaluation, "nested")).dbm(),
               -6.5, 1e-12);
    CHECK_NEAR(std::get<domain::PowerDelta>(result.evaluation->at(
                   program.calibrations.front().correction)).db(),
               0.75, 1e-12);

    try {
        (void)result.evaluation->at(ir::ValueId{program.values.size()});
        CHECK(false);
    } catch (const std::out_of_range&) {
    }
}

void expect_failure(ir::Program program, runtime::DiagnosticCode code,
                    parser::SourceSpan expected_span) {
    RecordingTester tester;
    RecordingMeasurement measurement{tester};
    const auto result = runtime::execute_point(
        program, domain::Frequency::from_hertz(1.5e9), tester, measurement);
    CHECK(!result.ok());
    CHECK(!result.evaluation.has_value());
    CHECK_EQ(result.diagnostics.front().code, code);
    CHECK_EQ(result.diagnostics.front().span, expected_span);
    CHECK(tester.calls.empty());
}

void defensive_validation_test() {
    const auto base = lower_source(operation_source);

    auto broken = base;
    broken.values[3].id = ir::ValueId{99};
    expect_failure(broken, runtime::DiagnosticCode::InvalidValueId,
                   broken.values[3].span);

    broken = base;
    std::get<ir::Alias>(broken.values[6].definition).value = ir::ValueId{999};
    expect_failure(broken, runtime::DiagnosticCode::InvalidValueId,
                   broken.values[6].span);

    broken = base;
    std::get<ir::Alias>(broken.values[6].definition).value = broken.values[6].id;
    expect_failure(broken, runtime::DiagnosticCode::InvalidDefinitionOrder,
                   broken.values[6].span);

    broken = base;
    broken.values[3].type = ir::ValueType::Power;
    expect_failure(broken, runtime::DiagnosticCode::TypeMismatch,
                   broken.values[3].span);

    broken = base;
    broken.reference = broken.measurement;
    expect_failure(broken, runtime::DiagnosticCode::InvalidProgramBinding,
                   broken.span);

    broken = base;
    broken.values.push_back(broken.values.front());
    broken.values.back().id = ir::ValueId{broken.values.size() - 1};
    expect_failure(broken, runtime::DiagnosticCode::InvalidProgramBinding,
                   broken.span);

    broken = base;
    broken.named_values.front().type = ir::ValueType::Power;
    expect_failure(broken, runtime::DiagnosticCode::TypeMismatch,
                   broken.named_values.front().span);

    broken = base;
    broken.calibrations.front().indexes.clear();
    expect_failure(broken, runtime::DiagnosticCode::InvalidProgramBinding,
                   broken.calibrations.front().span);

    broken = base;
    broken.calibrations.front().correction = ir::ValueId{999};
    expect_failure(broken, runtime::DiagnosticCode::InvalidValueId,
                   broken.calibrations.front().span);
}

struct ThrowingMeasurement final : device::MeasurementDevice {
    domain::Power measurePower() override {
        throw std::runtime_error("measurement failure");
    }
};

void exception_propagation_test() {
    RecordingTester tester;
    ThrowingMeasurement measurement;
    try {
        (void)runtime::execute_point(lower_source(operation_source),
                                     domain::Frequency::from_hertz(1.5e9), tester,
                                     measurement);
        CHECK(false);
    } catch (const std::runtime_error& error) {
        CHECK_EQ(std::string{error.what()}, "measurement failure");
    }
}

void canonical_pipeline_test() {
    std::ifstream input(std::string{HORUSRF_SOURCE_DIR} +
                        "/examples/tx_path_characterization.hrf", std::ios::binary);
    CHECK(input.good());
    const std::string source{std::istreambuf_iterator<char>{input},
                             std::istreambuf_iterator<char>{}};
    const auto program = lower_source(source);
    device::SimulatedRfConnection rf_output;
    device::SimulatedRfTester tester{rf_output};
    device::SimulatedMeasurementDevice measurement{rf_output};
    const auto result = runtime::execute_point(
        program, domain::Frequency::from_hertz(2.425e9), tester, measurement);
    CHECK(result.ok());
    CHECK_EQ(result.evaluation->values.size(), std::size_t{4});
    CHECK_NEAR(result.evaluation->frequency.hertz(), 2.425e9, 1e-3);
    CHECK_NEAR(std::get<domain::Power>(result.evaluation->at(program.reference)).dbm(),
               -10.0, 1e-12);
    CHECK_NEAR(std::get<domain::Frequency>(result.evaluation->at(
                   program.sweep.frequency)).hertz(),
               2.425e9, 1e-3);
    CHECK_NEAR(std::get<domain::Power>(result.evaluation->at(program.measurement)).dbm(),
               -9.45, 1e-12);
    CHECK(program.named_values.empty());
    CHECK_EQ(program.calibrations.front().indexes.front(), program.sweep.frequency);
    CHECK_NEAR(std::get<domain::PowerDelta>(result.evaluation->at(
                   program.calibrations.front().correction)).db(),
               -0.55, 1e-12);
}

} // namespace

int main() {
    try {
        diagnostic_code_name_tests();
        operation_matrix_and_device_order_test();
        defensive_validation_test();
        exception_propagation_test();
        canonical_pipeline_test();
        std::cout << "All HorusRF runtime tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
