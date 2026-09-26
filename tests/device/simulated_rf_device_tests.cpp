#include "horusrf/device/simulated_rf_device.hpp"

#include "test_support.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace horusrf;

template <typename T>
concept ConfiguresTx = requires(T& equipment, domain::Frequency frequency,
                               domain::Power power) {
    equipment.setFrequency(frequency);
    equipment.setOutputPower(power);
};

template <typename T>
concept MeasuresPower = requires(T& equipment) {
    equipment.measurePower();
};

static_assert(ConfiguresTx<device::RfDevice>);
static_assert(!MeasuresPower<device::RfDevice>);
static_assert(!ConfiguresTx<device::MeasurementDevice>);
static_assert(MeasuresPower<device::MeasurementDevice>);
static_assert(std::is_base_of_v<device::RfDevice, device::SimulatedRfTester>);
static_assert(std::is_base_of_v<device::MeasurementDevice,
                                device::SimulatedMeasurementDevice>);
static_assert(!std::is_base_of_v<device::MeasurementDevice,
                                 device::SimulatedRfTester>);
static_assert(!std::is_base_of_v<device::RfDevice,
                                 device::SimulatedMeasurementDevice>);

void require_unconfigured_failure(bool set_frequency, bool set_power) {
    device::SimulatedRfConnection connection;
    device::SimulatedRfTester tester{connection};
    device::SimulatedMeasurementDevice measurement{connection};
    if (set_frequency) tester.setFrequency(domain::Frequency::from_hertz(2.4e9));
    if (set_power) tester.setOutputPower(domain::Power::from_dbm(-10.0));
    try {
        (void)measurement.measurePower();
        CHECK(false);
    } catch (const std::logic_error&) {
    }
}

double measurement(double frequency, double power = -10.0) {
    device::SimulatedRfConnection connection;
    device::SimulatedRfTester tester{connection};
    device::SimulatedMeasurementDevice instrument{connection};
    tester.setFrequency(domain::Frequency::from_hertz(frequency));
    tester.setOutputPower(domain::Power::from_dbm(power));
    return instrument.measurePower().dbm();
}

void configuration_contract_test() {
    require_unconfigured_failure(false, false);
    require_unconfigured_failure(true, false);
    require_unconfigured_failure(false, true);

    device::SimulatedRfConnection frequency_first_connection;
    device::SimulatedRfTester frequency_first{frequency_first_connection};
    device::SimulatedMeasurementDevice frequency_first_measurement{
        frequency_first_connection};
    frequency_first.setFrequency(domain::Frequency::from_hertz(2.425e9));
    frequency_first.setOutputPower(domain::Power::from_dbm(-10.0));
    device::SimulatedRfConnection power_first_connection;
    device::SimulatedRfTester power_first{power_first_connection};
    device::SimulatedMeasurementDevice power_first_measurement{power_first_connection};
    power_first.setOutputPower(domain::Power::from_dbm(-10.0));
    power_first.setFrequency(domain::Frequency::from_hertz(2.425e9));
    CHECK_NEAR(frequency_first_measurement.measurePower().dbm(),
               power_first_measurement.measurePower().dbm(), 1e-12);
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
    device::SimulatedRfConnection first_connection;
    device::SimulatedRfConnection second_connection;
    device::SimulatedRfTester first{first_connection};
    device::SimulatedRfTester second{second_connection};
    device::SimulatedMeasurementDevice first_measurement{first_connection};
    device::SimulatedMeasurementDevice second_measurement{second_connection};
    device::RfDevice& first_abstract = first;
    device::RfDevice& second_abstract = second;
    for (auto* abstract : {&first_abstract, &second_abstract}) {
        abstract->setFrequency(domain::Frequency::from_hertz(2.425e9));
        abstract->setOutputPower(domain::Power::from_dbm(-10.0));
    }
    const auto initial = first_measurement.measurePower();
    CHECK_EQ(initial, first_measurement.measurePower());
    CHECK_EQ(initial, second_measurement.measurePower());

    first_abstract.setFrequency(domain::Frequency::from_hertz(2.475e9));
    CHECK_NEAR(first_measurement.measurePower().dbm(), -9.85, 1e-12);
    first_abstract.setOutputPower(domain::Power::from_dbm(-5.0));
    CHECK_NEAR(first_measurement.measurePower().dbm(), -4.85, 1e-12);
}

struct DestructionProbe final : device::RfDevice {
    explicit DestructionProbe(bool& destroyed) : destroyed_(destroyed) {}
    ~DestructionProbe() override { destroyed_ = true; }
    void setFrequency(domain::Frequency) override {}
    void setOutputPower(domain::Power) override {}
    bool& destroyed_;
};

struct MeasurementDestructionProbe final : device::MeasurementDevice {
    explicit MeasurementDestructionProbe(bool& destroyed) : destroyed_(destroyed) {}
    ~MeasurementDestructionProbe() override { destroyed_ = true; }
    domain::Power measurePower() override { return domain::Power::from_dbm(0.0); }
    bool& destroyed_;
};

void virtual_destruction_test() {
    bool destroyed = false;
    std::unique_ptr<device::RfDevice> probe = std::make_unique<DestructionProbe>(destroyed);
    probe.reset();
    CHECK(destroyed);

    bool measurement_destroyed = false;
    std::unique_ptr<device::MeasurementDevice> measurement_probe =
        std::make_unique<MeasurementDestructionProbe>(measurement_destroyed);
    measurement_probe.reset();
    CHECK(measurement_destroyed);
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
