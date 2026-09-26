#include "horusrf/device/simulated_rf_device.hpp"

#include <cmath>
#include <stdexcept>

namespace horusrf::device {
namespace {

domain::Power actual_output_power(domain::Frequency frequency,
                                  domain::Power requested_power) {
    constexpr double pi = 3.141592653589793238462643383279502884;
    const double normalized = (frequency.hertz() - 2.40e9) / 100.0e6;
    const double error = 0.35 + 0.20 * std::sin(2.0 * pi * normalized);
    return domain::Power::from_dbm(requested_power.dbm() + error);
}

} // namespace

SimulatedRfTester::SimulatedRfTester(SimulatedRfConnection& connection) noexcept
    : connection_(connection) {}

void SimulatedRfTester::setFrequency(domain::Frequency frequency) {
    frequency_ = frequency;
    update_output();
}

void SimulatedRfTester::setOutputPower(domain::Power power) {
    requested_power_ = power;
    update_output();
}

void SimulatedRfTester::update_output() {
    if (frequency_ && requested_power_) {
        connection_.output_power_ = actual_output_power(*frequency_, *requested_power_);
    }
}

SimulatedMeasurementDevice::SimulatedMeasurementDevice(
    const SimulatedRfConnection& connection) noexcept
    : connection_(connection) {}

domain::Power SimulatedMeasurementDevice::measurePower() {
    if (!connection_.output_power_) {
        throw std::logic_error(
            "RF tester frequency and output power must be configured before measurement");
    }
    return *connection_.output_power_;
}

} // namespace horusrf::device
