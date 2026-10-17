#pragma once

#include <optional>

#include "horusrf/device/measurement_device.hpp"
#include "horusrf/device/rf_device.hpp"
#include "horusrf/device/temperature_controller.hpp"

namespace horusrf::device {

class SimulatedRfTester;
class SimulatedMeasurementDevice;
class SimulatedTemperatureController;

// Represents only the private connection state shared by the simulated
// instruments. Its state is deliberately inaccessible outside their implementations.
class SimulatedRfConnection final {
private:
    std::optional<domain::Frequency> frequency_;
    std::optional<domain::Power> requested_power_;
    std::optional<domain::Temperature> temperature_;

    friend class SimulatedRfTester;
    friend class SimulatedMeasurementDevice;
    friend class SimulatedTemperatureController;
};

class SimulatedRfTester final : public RfDevice {
public:
    explicit SimulatedRfTester(SimulatedRfConnection& connection) noexcept;

    void setFrequency(domain::Frequency frequency) override;
    void setOutputPower(domain::Power power) override;

private:
    SimulatedRfConnection& connection_;
};

class SimulatedTemperatureController final : public TemperatureController {
public:
    explicit SimulatedTemperatureController(SimulatedRfConnection& connection) noexcept;
    void setTemperature(domain::Temperature temperature) override;

private:
    SimulatedRfConnection& connection_;
};

class SimulatedMeasurementDevice final : public MeasurementDevice,
                                         public EvmMeasurementDevice,
                                         public FrequencyErrorMeasurementDevice {
public:
    explicit SimulatedMeasurementDevice(
        const SimulatedRfConnection& connection) noexcept;

    [[nodiscard]] domain::Power measurePower() override;
    [[nodiscard]] domain::Ratio measureEvm() override;
    [[nodiscard]] domain::FrequencyDelta measureFrequencyError() override;

private:
    const SimulatedRfConnection& connection_;
};

} // namespace horusrf::device
