#include "horusrf/runtime/characterization.hpp"

#include "test_support.hpp"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "horusrf/device/simulated_rf_device.hpp"
#include "horusrf/ir/lowering.hpp"
#include "horusrf/parser/parser.hpp"
#include "horusrf/semantic/analyzer.hpp"

namespace {
using namespace horusrf;

constexpr auto small_source = R"(
characterize small {
  reference power = -10 dBm
  sweep frequency 0 Hz .. 1 Hz step 0.25 Hz
  measure power
  derive calibration deliberately_different {
    correction = 2 dB over frequency
  }
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
        frequencies.push_back(value);
        current_frequency = value;
    }
    void setOutputPower(domain::Power value) override {
        calls.push_back("power");
        powers.push_back(value);
    }
    std::vector<std::string> calls;
    std::vector<domain::Frequency> frequencies;
    std::vector<domain::Power> powers;
    domain::Frequency current_frequency = domain::Frequency::from_hertz(0.0);
};

struct RecordingMeasurement : device::MeasurementDevice {
    explicit RecordingMeasurement(RecordingTester& tester) : tester_(tester) {}

    domain::Power measurePower() override {
        tester_.calls.push_back("measure");
        ++measurements;
        if (throw_on_measurement != 0 && measurements == throw_on_measurement) {
            throw std::runtime_error("measurement failure");
        }
        return domain::Power::from_dbm(
            -9.5 + tester_.current_frequency.hertz() * 0.01);
    }

    RecordingTester& tester_;
    std::size_t measurements{};
    std::size_t throw_on_measurement{};
};

struct RecordingTemperature : device::TemperatureController {
    explicit RecordingTemperature(RecordingTester& tester) : tester_(tester) {}

    void setTemperature(domain::Temperature value) override {
        tester_.calls.push_back("temperature");
        temperatures.push_back(value);
    }

    RecordingTester& tester_;
    std::vector<domain::Temperature> temperatures;
};

struct RecordingExtendedMeasurements : device::EvmMeasurementDevice,
                                       device::FrequencyErrorMeasurementDevice {
    explicit RecordingExtendedMeasurements(RecordingTester& tester) : tester_(tester) {}
    domain::Ratio measureEvm() override {
        tester_.calls.push_back("evm");
        ++evm_count;
        return domain::Ratio::from_ratio(0.025);
    }
    domain::FrequencyDelta measureFrequencyError() override {
        tester_.calls.push_back("frequency_error");
        ++frequency_error_count;
        return domain::FrequencyDelta::from_hertz(1250.0);
    }
    RecordingTester& tester_;
    std::size_t evm_count{};
    std::size_t frequency_error_count{};
};

struct RecordingSetup {
    RecordingSetup() : measurement(tester), temperature(tester) {}

    RecordingTester tester;
    RecordingMeasurement measurement;
    RecordingTemperature temperature;
};

void set_sweep(ir::Program& program, double start, double end, double step) {
    program.sweep.start = domain::Frequency::from_hertz(start);
    program.sweep.end = domain::Frequency::from_hertz(end);
    program.sweep.step = domain::Frequency::from_hertz(step);
}

runtime::CharacterizationExecutionResult execute_range(double start, double end,
                                                        double step,
                                                        RecordingSetup& setup) {
    auto program = lower_source(small_source);
    set_sweep(program, start, end, step);
    return runtime::execute_characterization(program, setup.tester,
                                             setup.measurement);
}

void sweep_generation_test() {
    RecordingSetup one_setup;
    const auto one = execute_range(1.0, 1.0, 1.0, one_setup);
    CHECK(one.ok());
    CHECK_EQ(one.result->samples.size(), std::size_t{1});

    RecordingSetup decimal_setup;
    const auto decimal = execute_range(0.0, 1.0, 0.1, decimal_setup);
    CHECK(decimal.ok());
    CHECK_EQ(decimal.result->samples.size(), std::size_t{11});
    CHECK_NEAR(decimal.result->samples.back().frequency.hertz(), 1.0, 0.0);

    RecordingSetup non_divisible_setup;
    const auto non_divisible = execute_range(0.0, 1.0, 0.3, non_divisible_setup);
    CHECK(non_divisible.ok());
    CHECK_EQ(non_divisible.result->samples.size(), std::size_t{4});
    CHECK_NEAR(non_divisible.result->samples.back().frequency.hertz(), 0.9, 1e-12);

    RecordingSetup negative_setup;
    const auto negative = execute_range(-2.0, 0.0, 1.0, negative_setup);
    CHECK(negative.ok());
    CHECK_EQ(negative.result->samples.size(), std::size_t{3});
    CHECK_NEAR(negative.result->samples.front().frequency.hertz(), -2.0, 0.0);
    CHECK_NEAR(negative.result->samples.back().frequency.hertz(), 0.0, 0.0);
}

void orchestration_test() {
    RecordingSetup setup;
    const auto result = execute_range(0.0, 1.0, 0.25, setup);
    CHECK(result.ok());
    CHECK_EQ(result.result->samples.size(), std::size_t{5});
    CHECK_EQ(result.result->calibration.name, std::string{"deliberately_different"});
    CHECK_EQ(result.result->calibration.dimensions.size(), std::size_t{5});
    CHECK_EQ(result.result->calibration.corrections.size(), std::size_t{5});
    CHECK_EQ(setup.tester.calls.size(), std::size_t{15});
    for (std::size_t index = 0; index < result.result->samples.size(); ++index) {
        const auto& sample = result.result->samples[index];
        CHECK_EQ(setup.tester.calls[index * 3], std::string{"frequency"});
        CHECK_EQ(setup.tester.calls[index * 3 + 1], std::string{"power"});
        CHECK_EQ(setup.tester.calls[index * 3 + 2], std::string{"measure"});
        CHECK_EQ(setup.tester.frequencies[index], sample.frequency);
        CHECK_EQ(setup.tester.powers[index], sample.reference_power);
        CHECK_EQ(result.result->calibration.dimensions[index], sample.frequency);
        CHECK_EQ(result.result->calibration.corrections[index], sample.correction);
        CHECK_NEAR(sample.error.db(),
                   sample.measured_power.dbm() - sample.reference_power.dbm(), 1e-12);
        CHECK_NEAR(sample.correction.db(), 2.0, 1e-12);
        CHECK_NEAR(sample.corrected_power.dbm(), sample.measured_power.dbm() + 2.0,
                   1e-12);
        CHECK_NEAR(sample.residual_error.db(),
                   sample.corrected_power.dbm() - sample.reference_power.dbm(),
                   1e-12);
    }

    RecordingSetup repeated_setup;
    const auto repeated = execute_range(0.0, 1.0, 0.25, repeated_setup);
    CHECK(repeated.ok());
    for (std::size_t index = 0; index < result.result->samples.size(); ++index) {
        CHECK_EQ(repeated.result->samples[index].measured_power,
                 result.result->samples[index].measured_power);
        CHECK_EQ(repeated.result->samples[index].correction,
                 result.result->samples[index].correction);
    }
}

void expect_preflight_failure(ir::Program program, runtime::DiagnosticCode code) {
    RecordingSetup setup;
    const auto result = runtime::execute_characterization(
        program, setup.tester, setup.measurement);
    CHECK(!result.ok());
    CHECK(!result.result.has_value());
    CHECK(!result.diagnostics.empty());
    CHECK_EQ(result.diagnostics.front().code, code);
    CHECK(setup.tester.calls.empty());
}

void validation_test() {
    const auto base = lower_source(small_source);

    auto broken = base;
    broken.values.front().id = ir::ValueId{99};
    expect_preflight_failure(broken, runtime::DiagnosticCode::InvalidValueId);

    broken = base;
    broken.calibration.name.clear();
    expect_preflight_failure(broken, runtime::DiagnosticCode::InvalidCalibrationBinding);

    broken = base;
    broken.calibration.indexes.clear();
    expect_preflight_failure(broken, runtime::DiagnosticCode::InvalidProgramBinding);

    broken = base;
    set_sweep(broken, 0.0, 1.0, 0.0);
    expect_preflight_failure(broken, runtime::DiagnosticCode::InvalidSweep);

    broken = base;
    set_sweep(broken, 0.0, 1.0, -1.0);
    expect_preflight_failure(broken, runtime::DiagnosticCode::InvalidSweep);

    broken = base;
    set_sweep(broken, 2.0, 1.0, 1.0);
    expect_preflight_failure(broken, runtime::DiagnosticCode::InvalidSweep);

    broken = base;
    set_sweep(broken, 0.0, 1'000'000.0, 1.0);
    expect_preflight_failure(broken, runtime::DiagnosticCode::SweepTooLarge);

    broken = base;
    set_sweep(broken, -std::numeric_limits<double>::max(),
              std::numeric_limits<double>::max(),
              std::numeric_limits<double>::denorm_min());
    expect_preflight_failure(broken, runtime::DiagnosticCode::SweepTooLarge);
}

void exception_propagation_test() {
    auto program = lower_source(small_source);
    RecordingSetup setup;
    setup.measurement.throw_on_measurement = 2;
    try {
        (void)runtime::execute_characterization(program, setup.tester,
                                                setup.measurement);
        CHECK(false);
    } catch (const std::runtime_error& error) {
        CHECK_EQ(std::string{error.what()}, std::string{"measurement failure"});
        CHECK_EQ(setup.measurement.measurements, std::size_t{2});
        CHECK_EQ(setup.tester.calls.size(), std::size_t{6});
    }
}

void multidimensional_execution_test() {
    const auto program = lower_source(R"(
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
    RecordingSetup setup;
    const auto unavailable = runtime::execute_characterization(
        program, setup.tester, setup.measurement);
    CHECK(!unavailable.ok());
    CHECK(!unavailable.result.has_value());
    CHECK_EQ(unavailable.diagnostics.size(), std::size_t{1});
    CHECK_EQ(unavailable.diagnostics.front().code,
             runtime::DiagnosticCode::CapabilityUnavailable);
    CHECK(setup.tester.calls.empty());
    CHECK_EQ(setup.measurement.measurements, std::size_t{0});

    RecordingSetup missing_power;
    const auto power_unavailable = runtime::execute_characterization(
        program, runtime::DeviceCapabilities{missing_power.tester, nullptr,
                                             &missing_power.temperature});
    CHECK(!power_unavailable.ok());
    CHECK_EQ(power_unavailable.diagnostics.front().code,
             runtime::DiagnosticCode::CapabilityUnavailable);
    CHECK(missing_power.tester.calls.empty());

    auto inconsistent = program;
    inconsistent.sweeps[1].point_count = 5;
    inconsistent.total_point_count = 105;
    RecordingSetup inconsistent_setup;
    const auto inconsistent_execution = runtime::execute_characterization(
        inconsistent,
        runtime::DeviceCapabilities{inconsistent_setup.tester,
                                    &inconsistent_setup.measurement,
                                    &inconsistent_setup.temperature});
    CHECK(!inconsistent_execution.ok());
    CHECK_EQ(inconsistent_execution.diagnostics.front().code,
             runtime::DiagnosticCode::InvalidProgramBinding);
    CHECK(inconsistent_setup.tester.calls.empty());

    const auto execution = runtime::execute_characterization(
        program, runtime::DeviceCapabilities{setup.tester, &setup.measurement,
                                             &setup.temperature});
    CHECK(execution.ok());
    CHECK_EQ(execution.result->samples.size(), std::size_t{126});
    CHECK_EQ(execution.result->calibration.operating_points.size(),
             std::size_t{126});
    CHECK_EQ(setup.measurement.measurements, std::size_t{126});
    CHECK_EQ(setup.tester.calls.size(), std::size_t{126 * 4});
    CHECK_EQ(setup.temperature.temperatures.size(), std::size_t{126});

    for (std::size_t index = 0; index < 126; ++index) {
        const auto& sample = execution.result->samples[index];
        CHECK_EQ(sample.operating_point.coordinates.size(), std::size_t{2});
        const auto& frequency = sample.operating_point.coordinates[0];
        const auto& temperature = sample.operating_point.coordinates[1];
        CHECK_EQ(frequency.name, std::string{"frequency"});
        CHECK_EQ(frequency.type, domain::CoordinateType::Frequency);
        CHECK_EQ(temperature.name, std::string{"temperature"});
        CHECK_EQ(temperature.type, domain::CoordinateType::Temperature);
        CHECK_NEAR(std::get<domain::Frequency>(frequency.value).hertz(),
                   2.40e9 + static_cast<double>(index / 6) * 5.0e6, 1e-3);
        CHECK_NEAR(std::get<domain::Temperature>(temperature.value).kelvin(),
                   253.15 + static_cast<double>(index % 6) * 20.0, 1e-12);
        CHECK_EQ(execution.result->calibration.operating_points[index],
                 sample.operating_point);
        CHECK_EQ(setup.tester.calls[index * 4], std::string{"frequency"});
        CHECK_EQ(setup.tester.calls[index * 4 + 1], std::string{"temperature"});
        CHECK_EQ(setup.tester.calls[index * 4 + 2], std::string{"power"});
        CHECK_EQ(setup.tester.calls[index * 4 + 3], std::string{"measure"});
    }

    RecordingSetup failing;
    failing.measurement.throw_on_measurement = 2;
    try {
        (void)runtime::execute_characterization(
            program, runtime::DeviceCapabilities{failing.tester,
                                                 &failing.measurement,
                                                 &failing.temperature});
        CHECK(false);
    } catch (const std::runtime_error&) {
        CHECK_EQ(failing.measurement.measurements, std::size_t{2});
        CHECK_EQ(failing.tester.calls.size(), std::size_t{8});
    }
}

void canonical_pipeline_test() {
    std::ifstream input(std::string{HORUSRF_SOURCE_DIR} +
                        "/examples/tx_path_characterization.hrf", std::ios::binary);
    CHECK(input.good());
    const std::string source{std::istreambuf_iterator<char>{input},
                             std::istreambuf_iterator<char>{}};
    const auto program = lower_source(source);
    CHECK_EQ(program.calibration.indexes.front(), program.sweep.frequency);

    device::SimulatedRfConnection rf_output;
    device::SimulatedRfTester tester{rf_output};
    device::SimulatedMeasurementDevice measurement{rf_output};
    const auto execution =
        runtime::execute_characterization(program, tester, measurement);
    CHECK(execution.ok());
    const auto& result = *execution.result;
    CHECK_EQ(result.samples.size(), std::size_t{101});
    CHECK_EQ(result.calibration.name, std::string{"tx_power"});
    CHECK_EQ(result.calibration.dimensions.size(), result.samples.size());
    CHECK_EQ(result.calibration.corrections.size(), result.samples.size());
    CHECK_NEAR(result.samples.front().frequency.hertz(), 2.40e9, 1e-3);
    CHECK_NEAR(result.samples[50].frequency.hertz(), 2.45e9, 1e-3);
    CHECK_NEAR(result.samples.back().frequency.hertz(), 2.50e9, 1e-3);
    CHECK_NEAR(result.samples.front().error.db(), 0.35, 1e-12);
    CHECK_NEAR(result.samples[25].error.db(), 0.55, 1e-12);
    CHECK_NEAR(result.samples[50].error.db(), 0.35, 1e-12);
    CHECK_NEAR(result.samples[75].error.db(), 0.15, 1e-12);
    CHECK_NEAR(result.samples.back().error.db(), 0.35, 1e-12);
    for (const auto& sample : result.samples) {
        CHECK_NEAR(sample.reference_power.dbm(), -10.0, 1e-12);
        CHECK_NEAR(sample.correction.db(), -sample.error.db(), 1e-12);
        CHECK_NEAR(sample.corrected_power.dbm(), -10.0, 1e-12);
        CHECK_NEAR(sample.residual_error.db(), 0.0, 1e-12);
    }
    CHECK(result.metrics.corrected_rms_error_db < 1e-12);
    CHECK(result.metrics.corrected_rms_error_db <
          0.1 * result.metrics.uncorrected_rms_error_db);
}

void named_measurement_execution_test() {
    const auto program = lower_source(R"(
characterize named {
 reference power = -10 dBm
 sweep frequency 2.40 GHz .. 2.50 GHz step 5 MHz
 sweep temperature -20 degC .. 80 degC step 20 deltaC
 measure output_power = power
 measure evm_result = evm
 measure carrier_error = frequency_error
 derive power_correction = reference.power - output_power
 derive calibration cal { correction = power_correction over frequency, temperature }
})");
    RecordingSetup setup;
    RecordingExtendedMeasurements extended{setup.tester};

    const auto unavailable = runtime::execute_characterization(
        program, runtime::DeviceCapabilities{setup.tester, &setup.measurement,
                                             &setup.temperature, nullptr, &extended});
    CHECK(!unavailable.ok());
    CHECK_EQ(unavailable.diagnostics.front().code,
             runtime::DiagnosticCode::CapabilityUnavailable);
    CHECK(setup.tester.calls.empty());

    const auto execution = runtime::execute_characterization(
        program, runtime::DeviceCapabilities{setup.tester, &setup.measurement,
                                             &setup.temperature, &extended, &extended});
    CHECK(execution.ok());
    CHECK_EQ(execution.result->samples.size(), std::size_t{126});
    CHECK_EQ(setup.measurement.measurements, std::size_t{126});
    CHECK_EQ(extended.evm_count, std::size_t{126});
    CHECK_EQ(extended.frequency_error_count, std::size_t{126});
    CHECK_EQ(setup.tester.calls.size(), std::size_t{126 * 6});
    for (std::size_t point = 0; point < 126; ++point) {
        const auto offset = point * 6;
        CHECK_EQ(setup.tester.calls[offset], std::string{"frequency"});
        CHECK_EQ(setup.tester.calls[offset + 1], std::string{"temperature"});
        CHECK_EQ(setup.tester.calls[offset + 2], std::string{"power"});
        CHECK_EQ(setup.tester.calls[offset + 3], std::string{"measure"});
        CHECK_EQ(setup.tester.calls[offset + 4], std::string{"evm"});
        CHECK_EQ(setup.tester.calls[offset + 5], std::string{"frequency_error"});
    }
    for (const auto& sample : execution.result->samples) {
        CHECK_EQ(sample.observations.size(), std::size_t{3});
        CHECK_EQ(sample.observations[0].name, std::string{"output_power"});
        CHECK_EQ(sample.observations[1].type, domain::ObservationType::Ratio);
        CHECK_EQ(sample.observations[1].canonical_unit, std::string{"ratio"});
        CHECK_EQ(sample.observations[2].type,
                 domain::ObservationType::FrequencyDelta);
        CHECK_EQ(sample.observations[2].canonical_unit, std::string{"Hz"});
    }
    CHECK_EQ(execution.result->samples.size() *
                 execution.result->samples.front().observations.size(),
             std::size_t{378});
}

} // namespace

int main() {
    try {
        sweep_generation_test();
        orchestration_test();
        validation_test();
        exception_propagation_test();
        multidimensional_execution_test();
        canonical_pipeline_test();
        named_measurement_execution_test();
        std::cout << "All HorusRF characterization tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
