#include "horusrf/device/simulated_rf_device.hpp"

#include <cmath>
#include <stdexcept>

namespace horusrf::device {
namespace {

domain::Power actual_output_power(domain::Frequency frequency,
                                  domain::Power requested_power,
                                  domain::Temperature temperature) {
    constexpr double pi = 3.141592653589793238462643383279502884;
    const double normalized = (frequency.hertz() - 2.40e9) / 100.0e6;
    const double temperature_error = 0.002 * (temperature.kelvin() - 293.15);
    const double error = 0.35 + 0.20 * std::sin(2.0 * pi * normalized) +
                         temperature_error;
    return domain::Power::from_dbm(requested_power.dbm() + error);
}

} // namespace

SimulatedRfTester::SimulatedRfTester(SimulatedRfConnection& connection) noexcept
    : connection_(connection) {}

void SimulatedRfTester::setFrequency(domain::Frequency frequency) {
    connection_.frequency_ = frequency;
}

void SimulatedRfTester::setOutputPower(domain::Power power) {
    connection_.requested_power_ = power;
}

SimulatedTemperatureController::SimulatedTemperatureController(
    SimulatedRfConnection& connection) noexcept
    : connection_(connection) {}

void SimulatedTemperatureController::setTemperature(domain::Temperature temperature) {
    connection_.temperature_ = temperature;
}

SimulatedMeasurementDevice::SimulatedMeasurementDevice(
    const SimulatedRfConnection& connection) noexcept
    : connection_(connection) {}

domain::Power SimulatedMeasurementDevice::measurePower() {
    if (!connection_.frequency_ || !connection_.requested_power_) {
        throw std::logic_error(
            "RF tester frequency and output power must be configured before measurement");
    }
    const auto temperature = connection_.temperature_.value_or(
        domain::Temperature::from_kelvin(293.15));
    return actual_output_power(*connection_.frequency_, *connection_.requested_power_,
                               temperature);
}

} // namespace horusrf::device
