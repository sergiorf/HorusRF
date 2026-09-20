#include "horusrf/device/simulated_rf_device.hpp"

#include <cmath>
#include <stdexcept>

namespace horusrf::device {

void SimulatedRfDevice::setFrequency(domain::Frequency frequency) {
    frequency_ = frequency;
}

void SimulatedRfDevice::setOutputPower(domain::Power power) {
    output_power_ = power;
}

domain::Power SimulatedRfDevice::measurePower() {
    if (!frequency_ || !output_power_) {
        throw std::logic_error(
            "frequency and output power must be configured before measurement");
    }

    constexpr double pi = 3.141592653589793238462643383279502884;
    const double normalized = (frequency_->hertz() - 2.40e9) / 100.0e6;
    const double error = 0.35 + 0.20 * std::sin(2.0 * pi * normalized);
    return domain::Power::from_dbm(output_power_->dbm() + error);
}

} // namespace horusrf::device
