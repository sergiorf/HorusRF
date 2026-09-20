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

struct RecordingDevice : device::RfDevice {
    void setFrequency(domain::Frequency value) override {
        calls.push_back("frequency");
        frequencies.push_back(value);
        current_frequency = value;
    }
    void setOutputPower(domain::Power value) override {
        calls.push_back("power");
        powers.push_back(value);
    }
    domain::Power measurePower() override {
        calls.push_back("measure");
        ++measurements;
        if (throw_on_measurement != 0 && measurements == throw_on_measurement) {
            throw std::runtime_error("device failure");
        }
        return domain::Power::from_dbm(-9.5 + current_frequency.hertz() * 0.01);
    }

    std::vector<std::string> calls;
    std::vector<domain::Frequency> frequencies;
    std::vector<domain::Power> powers;
    domain::Frequency current_frequency = domain::Frequency::from_hertz(0.0);
    std::size_t measurements{};
    std::size_t throw_on_measurement{};
};

void set_sweep(ir::Program& program, double start, double end, double step) {
    program.sweep.start = domain::Frequency::from_hertz(start);
    program.sweep.end = domain::Frequency::from_hertz(end);
    program.sweep.step = domain::Frequency::from_hertz(step);
}

runtime::CharacterizationExecutionResult execute_range(double start, double end,
                                                        double step,
                                                        RecordingDevice& device) {
    auto program = lower_source(small_source);
    set_sweep(program, start, end, step);
    return runtime::execute_characterization(program, device);
}

void sweep_generation_test() {
    RecordingDevice one_device;
    const auto one = execute_range(1.0, 1.0, 1.0, one_device);
    CHECK(one.ok());
    CHECK_EQ(one.result->samples.size(), std::size_t{1});

    RecordingDevice decimal_device;
    const auto decimal = execute_range(0.0, 1.0, 0.1, decimal_device);
    CHECK(decimal.ok());
    CHECK_EQ(decimal.result->samples.size(), std::size_t{11});
    CHECK_NEAR(decimal.result->samples.back().frequency.hertz(), 1.0, 0.0);

    RecordingDevice non_divisible_device;
    const auto non_divisible = execute_range(0.0, 1.0, 0.3, non_divisible_device);
    CHECK(non_divisible.ok());
    CHECK_EQ(non_divisible.result->samples.size(), std::size_t{4});
    CHECK_NEAR(non_divisible.result->samples.back().frequency.hertz(), 0.9, 1e-12);

    RecordingDevice negative_device;
    const auto negative = execute_range(-2.0, 0.0, 1.0, negative_device);
    CHECK(negative.ok());
    CHECK_EQ(negative.result->samples.size(), std::size_t{3});
    CHECK_NEAR(negative.result->samples.front().frequency.hertz(), -2.0, 0.0);
    CHECK_NEAR(negative.result->samples.back().frequency.hertz(), 0.0, 0.0);
}

void orchestration_test() {
    RecordingDevice device;
    const auto result = execute_range(0.0, 1.0, 0.25, device);
    CHECK(result.ok());
    CHECK_EQ(result.result->samples.size(), std::size_t{5});
    CHECK_EQ(result.result->calibration.name, std::string{"deliberately_different"});
    CHECK_EQ(result.result->calibration.dimensions.size(), std::size_t{5});
    CHECK_EQ(result.result->calibration.corrections.size(), std::size_t{5});
    CHECK_EQ(device.calls.size(), std::size_t{15});
    for (std::size_t index = 0; index < result.result->samples.size(); ++index) {
        const auto& sample = result.result->samples[index];
        CHECK_EQ(device.calls[index * 3], std::string{"frequency"});
        CHECK_EQ(device.calls[index * 3 + 1], std::string{"power"});
        CHECK_EQ(device.calls[index * 3 + 2], std::string{"measure"});
        CHECK_EQ(device.frequencies[index], sample.frequency);
        CHECK_EQ(device.powers[index], sample.reference_power);
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

    RecordingDevice repeated_device;
    const auto repeated = execute_range(0.0, 1.0, 0.25, repeated_device);
    CHECK(repeated.ok());
    for (std::size_t index = 0; index < result.result->samples.size(); ++index) {
        CHECK_EQ(repeated.result->samples[index].measured_power,
                 result.result->samples[index].measured_power);
        CHECK_EQ(repeated.result->samples[index].correction,
                 result.result->samples[index].correction);
    }
}

void expect_preflight_failure(ir::Program program, runtime::DiagnosticCode code) {
    RecordingDevice device;
    const auto result = runtime::execute_characterization(program, device);
    CHECK(!result.ok());
    CHECK(!result.result.has_value());
    CHECK(!result.diagnostics.empty());
    CHECK_EQ(result.diagnostics.front().code, code);
    CHECK(device.calls.empty());
}

void validation_test() {
    const auto base = lower_source(small_source);

    auto broken = base;
    broken.values.front().id = ir::ValueId{99};
    expect_preflight_failure(broken, runtime::DiagnosticCode::InvalidValueId);

    broken = base;
    broken.calibrations.clear();
    expect_preflight_failure(broken, runtime::DiagnosticCode::InvalidCalibrationBinding);

    broken = base;
    broken.calibrations.push_back(broken.calibrations.front());
    expect_preflight_failure(broken, runtime::DiagnosticCode::InvalidCalibrationBinding);

    broken = base;
    broken.calibrations.front().name.clear();
    expect_preflight_failure(broken, runtime::DiagnosticCode::InvalidCalibrationBinding);

    broken = base;
    broken.calibrations.front().indexes.clear();
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
    RecordingDevice device;
    device.throw_on_measurement = 2;
    try {
        (void)runtime::execute_characterization(program, device);
        CHECK(false);
    } catch (const std::runtime_error& error) {
        CHECK_EQ(std::string{error.what()}, std::string{"device failure"});
        CHECK_EQ(device.measurements, std::size_t{2});
        CHECK_EQ(device.calls.size(), std::size_t{6});
    }
}

void canonical_pipeline_test() {
    std::ifstream input(std::string{HORUSRF_SOURCE_DIR} +
                        "/examples/tx_path_characterization.hrf", std::ios::binary);
    CHECK(input.good());
    const std::string source{std::istreambuf_iterator<char>{input},
                             std::istreambuf_iterator<char>{}};
    const auto program = lower_source(source);
    CHECK_EQ(program.calibrations.front().indexes.front(), program.sweep.frequency);

    device::SimulatedRfDevice simulator;
    const auto execution = runtime::execute_characterization(program, simulator);
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

} // namespace

int main() {
    try {
        sweep_generation_test();
        orchestration_test();
        validation_test();
        exception_propagation_test();
        canonical_pipeline_test();
        std::cout << "All HorusRF characterization tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
