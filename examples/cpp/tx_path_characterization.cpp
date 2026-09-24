#include "tx_path_characterization.hpp"

#include <cstddef>

namespace horusrf::examples {
namespace {

constexpr double reference_power_dbm = -10.0;
constexpr double first_frequency_hz = 2.40e9;
constexpr double last_frequency_hz = 2.50e9;
constexpr double frequency_step_hz = 1.0e6;
constexpr std::size_t point_count = 101;

static_assert(first_frequency_hz +
                  static_cast<double>(point_count - 1) * frequency_step_hz ==
              last_frequency_hz);

} // namespace

domain::CharacterizationResult
run_tx_path_characterization(device::RfDevice& device) {
    domain::CharacterizationResult result;
    result.calibration.name = "tx_power";
    result.samples.reserve(point_count);
    result.calibration.dimensions.reserve(point_count);
    result.calibration.corrections.reserve(point_count);

    const auto reference_power = domain::Power::from_dbm(reference_power_dbm);

    for (std::size_t index = 0; index < point_count; ++index) {
        const auto frequency = domain::Frequency::from_hertz(
            first_frequency_hz + static_cast<double>(index) * frequency_step_hz);

        device.setFrequency(frequency);
        device.setOutputPower(reference_power);
        const auto measured_power = device.measurePower();

        const auto error = measured_power - reference_power;
        const auto correction = -error;

        result.samples.push_back(domain::make_characterization_sample(
            frequency, reference_power, measured_power, correction));
        result.calibration.dimensions.push_back(frequency);
        result.calibration.corrections.push_back(correction);
    }

    result.metrics = domain::calculate_metrics(result.samples);
    return result;
}

} // namespace horusrf::examples
