#include "horusrf/device/simulated_rf_device.hpp"

#include "test_support.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace horusrf;

void require_unconfigured_failure(bool set_frequency, bool set_power) {
    device::SimulatedRfDevice simulator;
    device::RfDevice& abstract = simulator;
    if (set_frequency) abstract.setFrequency(domain::Frequency::from_hertz(2.4e9));
    if (set_power) abstract.setOutputPower(domain::Power::from_dbm(-10.0));
    try {
        (void)abstract.measurePower();
        CHECK(false);
    } catch (const std::logic_error&) {
    }
}

double measurement(double frequency, double power = -10.0) {
    device::SimulatedRfDevice simulator;
    device::RfDevice& abstract = simulator;
    abstract.setFrequency(domain::Frequency::from_hertz(frequency));
    abstract.setOutputPower(domain::Power::from_dbm(power));
    return abstract.measurePower().dbm();
}

void configuration_contract_test() {
    require_unconfigured_failure(false, false);
    require_unconfigured_failure(true, false);
    require_unconfigured_failure(false, true);

    device::SimulatedRfDevice frequency_first;
    frequency_first.setFrequency(domain::Frequency::from_hertz(2.425e9));
    frequency_first.setOutputPower(domain::Power::from_dbm(-10.0));
    device::SimulatedRfDevice power_first;
    power_first.setOutputPower(domain::Power::from_dbm(-10.0));
    power_first.setFrequency(domain::Frequency::from_hertz(2.425e9));
    CHECK_NEAR(frequency_first.measurePower().dbm(), power_first.measurePower().dbm(), 1e-12);
}

void documented_model_test() {
    CHECK_NEAR(measurement(2.400e9), -9.65, 1e-12);
    CHECK_NEAR(measurement(2.425e9), -9.45, 1e-12);
    CHECK_NEAR(measurement(2.450e9), -9.65, 1e-12);
    CHECK_NEAR(measurement(2.475e9), -9.85, 1e-12);
    CHECK_NEAR(measurement(2.500e9), -9.65, 1e-12);
    CHECK_NEAR(measurement(2.425e9, -7.0) - measurement(2.425e9, -10.0), 3.0,
               1e-12);
}

void determinism_and_reconfiguration_test() {
    device::SimulatedRfDevice first;
    device::SimulatedRfDevice second;
    device::RfDevice& first_abstract = first;
    device::RfDevice& second_abstract = second;
    for (auto* abstract : {&first_abstract, &second_abstract}) {
        abstract->setFrequency(domain::Frequency::from_hertz(2.425e9));
        abstract->setOutputPower(domain::Power::from_dbm(-10.0));
    }
    const auto initial = first_abstract.measurePower();
    CHECK_EQ(initial, first_abstract.measurePower());
    CHECK_EQ(initial, second_abstract.measurePower());

    first_abstract.setFrequency(domain::Frequency::from_hertz(2.475e9));
    CHECK_NEAR(first_abstract.measurePower().dbm(), -9.85, 1e-12);
    first_abstract.setOutputPower(domain::Power::from_dbm(-5.0));
    CHECK_NEAR(first_abstract.measurePower().dbm(), -4.85, 1e-12);
}

struct DestructionProbe final : device::RfDevice {
    explicit DestructionProbe(bool& destroyed) : destroyed_(destroyed) {}
    ~DestructionProbe() override { destroyed_ = true; }
    void setFrequency(domain::Frequency) override {}
    void setOutputPower(domain::Power) override {}
    domain::Power measurePower() override { return domain::Power::from_dbm(0.0); }
    bool& destroyed_;
};

void virtual_destruction_test() {
    bool destroyed = false;
    std::unique_ptr<device::RfDevice> probe = std::make_unique<DestructionProbe>(destroyed);
    probe.reset();
    CHECK(destroyed);
}

} // namespace

int main() {
    try {
        configuration_contract_test();
        documented_model_test();
        determinism_and_reconfiguration_test();
        virtual_destruction_test();
        std::cout << "All HorusRF simulator tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
