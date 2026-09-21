#include "tx_path_characterization.hpp"

#include "test_support.hpp"

#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "horusrf/device/simulated_rf_device.hpp"

namespace {
using namespace horusrf;

enum class Call {
    set_frequency,
    set_output_power,
    measure_power,
};

class DeviceFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct RecordingDevice final : device::RfDevice {
    void setFrequency(domain::Frequency frequency) override {
        calls.push_back(Call::set_frequency);
        frequencies.push_back(frequency);
    }

    void setOutputPower(domain::Power power) override {
        calls.push_back(Call::set_output_power);
        output_powers.push_back(power);
    }

    domain::Power measurePower() override {
        calls.push_back(Call::measure_power);
        ++measurement_count;
        if (throw_on_measurement != 0 && measurement_count == throw_on_measurement) {
            throw DeviceFailure{"recording device measurement failed"};
        }

        const auto point_index = static_cast<double>(measurement_count - 1);
        const auto measured = domain::Power::from_dbm(-9.8 + point_index * 0.003);
        measurements.push_back(measured);
        return measured;
    }

    std::vector<Call> calls;
    std::vector<domain::Frequency> frequencies;
    std::vector<domain::Power> output_powers;
    std::vector<domain::Power> measurements;
    std::size_t measurement_count{};
    std::size_t throw_on_measurement{};
};

void check_metrics_equal(const domain::CharacterizationMetrics& actual,
                         const domain::CharacterizationMetrics& expected) {
    CHECK_EQ(actual.uncorrected_rms_error_db, expected.uncorrected_rms_error_db);
    CHECK_EQ(actual.corrected_rms_error_db, expected.corrected_rms_error_db);
    CHECK_EQ(actual.maximum_absolute_error_db, expected.maximum_absolute_error_db);
    CHECK_EQ(actual.corrected_maximum_absolute_error_db,
             expected.corrected_maximum_absolute_error_db);
    CHECK_EQ(actual.rms_improvement_ratio, expected.rms_improvement_ratio);
}

void recording_device_orchestration_test() {
    RecordingDevice device;
    const auto result = examples::run_tx_path_characterization(device);

    CHECK_EQ(device.calls.size(), std::size_t{303});
    CHECK_EQ(device.measurement_count, std::size_t{101});
    CHECK_EQ(device.frequencies.size(), std::size_t{101});
    CHECK_EQ(device.output_powers.size(), std::size_t{101});
    CHECK_EQ(device.measurements.size(), std::size_t{101});
    CHECK_EQ(result.samples.size(), std::size_t{101});
    CHECK_EQ(result.calibration.name, std::string{"tx_power"});
    CHECK_EQ(result.calibration.dimensions.size(), std::size_t{101});
    CHECK_EQ(result.calibration.corrections.size(), std::size_t{101});

    CHECK_NEAR(device.frequencies.front().hertz(), 2.40e9, 0.0);
    CHECK_NEAR(device.frequencies[1].hertz(), 2.401e9, 0.0);
    CHECK_NEAR(device.frequencies.back().hertz(), 2.50e9, 0.0);

    for (std::size_t index = 0; index < result.samples.size(); ++index) {
        const auto& sample = result.samples[index];
        CHECK_EQ(device.calls[index * 3], Call::set_frequency);
        CHECK_EQ(device.calls[index * 3 + 1], Call::set_output_power);
        CHECK_EQ(device.calls[index * 3 + 2], Call::measure_power);
        CHECK_EQ(device.frequencies[index], sample.frequency);
        CHECK_EQ(device.output_powers[index], sample.reference_power);
        CHECK_EQ(device.measurements[index], sample.measured_power);
        CHECK_NEAR(sample.frequency.hertz(),
                   2.40e9 + static_cast<double>(index) * 1.0e6, 0.0);
        CHECK_NEAR(sample.reference_power.dbm(), -10.0, 0.0);
        CHECK_NEAR(sample.error.db(),
                   sample.measured_power.dbm() - sample.reference_power.dbm(), 1e-12);
        CHECK_NEAR(sample.correction.db(),
                   sample.reference_power.dbm() - sample.measured_power.dbm(), 1e-12);
        CHECK_NEAR(sample.corrected_power.dbm(),
                   sample.measured_power.dbm() + sample.correction.db(), 1e-12);
        CHECK_NEAR(sample.residual_error.db(),
                   sample.corrected_power.dbm() - sample.reference_power.dbm(), 1e-12);
        CHECK_EQ(result.calibration.dimensions[index], sample.frequency);
        CHECK_EQ(result.calibration.corrections[index], sample.correction);
    }

    check_metrics_equal(result.metrics, domain::calculate_metrics(result.samples));
}

void exception_propagation_test() {
    RecordingDevice device;
    device.throw_on_measurement = 7;

    try {
        (void)examples::run_tx_path_characterization(device);
        CHECK(false);
    } catch (const DeviceFailure& error) {
        CHECK_EQ(std::string{error.what()},
                 std::string{"recording device measurement failed"});
        CHECK_EQ(device.measurement_count, std::size_t{7});
        CHECK_EQ(device.calls.size(), std::size_t{21});
        CHECK_EQ(device.frequencies.size(), std::size_t{7});
        CHECK_EQ(device.output_powers.size(), std::size_t{7});
        CHECK_EQ(device.measurements.size(), std::size_t{6});
    }
}

void check_canonical_result(const domain::CharacterizationResult& result) {
    CHECK_EQ(result.samples.size(), std::size_t{101});
    CHECK_EQ(result.calibration.name, std::string{"tx_power"});
    CHECK_EQ(result.calibration.dimensions.size(), result.samples.size());
    CHECK_EQ(result.calibration.corrections.size(), result.samples.size());
    CHECK_NEAR(result.samples.front().frequency.hertz(), 2.40e9, 0.0);
    CHECK_NEAR(result.samples[50].frequency.hertz(), 2.45e9, 0.0);
    CHECK_NEAR(result.samples.back().frequency.hertz(), 2.50e9, 0.0);
    CHECK_NEAR(result.samples.front().error.db(), 0.35, 1e-12);
    CHECK_NEAR(result.samples[25].error.db(), 0.55, 1e-12);
    CHECK_NEAR(result.samples[50].error.db(), 0.35, 1e-12);
    CHECK_NEAR(result.samples[75].error.db(), 0.15, 1e-12);
    CHECK_NEAR(result.samples.back().error.db(), 0.35, 1e-12);

    for (std::size_t index = 0; index < result.samples.size(); ++index) {
        const auto& sample = result.samples[index];
        CHECK_NEAR(sample.frequency.hertz(),
                   2.40e9 + static_cast<double>(index) * 1.0e6, 0.0);
        if (index != 0) {
            CHECK(sample.frequency > result.samples[index - 1].frequency);
            CHECK_NEAR(sample.frequency.hertz() -
                           result.samples[index - 1].frequency.hertz(),
                       1.0e6, 0.0);
        }
        CHECK_NEAR(sample.reference_power.dbm(), -10.0, 1e-12);
        CHECK_NEAR(sample.error.db(),
                   sample.measured_power.dbm() - sample.reference_power.dbm(), 1e-12);
        CHECK_NEAR(sample.correction.db(), -sample.error.db(), 1e-12);
        CHECK_NEAR(sample.corrected_power.dbm(), -10.0, 1e-12);
        CHECK_NEAR(sample.residual_error.db(), 0.0, 1e-12);
        CHECK_EQ(result.calibration.dimensions[index], sample.frequency);
        CHECK_EQ(result.calibration.corrections[index], sample.correction);
    }

    check_metrics_equal(result.metrics, domain::calculate_metrics(result.samples));
    CHECK(result.metrics.corrected_rms_error_db < 1e-12);
    CHECK(result.metrics.corrected_rms_error_db <
          0.1 * result.metrics.uncorrected_rms_error_db);
}

void simulator_acceptance_test() {
    device::SimulatedRfDevice first_device;
    const auto first = examples::run_tx_path_characterization(first_device);
    check_canonical_result(first);

    device::SimulatedRfDevice second_device;
    const auto second = examples::run_tx_path_characterization(second_device);
    check_canonical_result(second);

    for (std::size_t index = 0; index < first.samples.size(); ++index) {
        CHECK_EQ(second.samples[index].frequency, first.samples[index].frequency);
        CHECK_EQ(second.samples[index].reference_power,
                 first.samples[index].reference_power);
        CHECK_EQ(second.samples[index].measured_power,
                 first.samples[index].measured_power);
        CHECK_EQ(second.samples[index].error, first.samples[index].error);
        CHECK_EQ(second.samples[index].correction, first.samples[index].correction);
        CHECK_EQ(second.samples[index].corrected_power,
                 first.samples[index].corrected_power);
        CHECK_EQ(second.samples[index].residual_error,
                 first.samples[index].residual_error);
    }
    check_metrics_equal(second.metrics, first.metrics);
}

} // namespace

int main() {
    try {
        recording_device_orchestration_test();
        exception_propagation_test();
        simulator_acceptance_test();
        std::cout << "All HorusRF procedural example tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
