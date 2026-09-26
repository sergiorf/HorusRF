#pragma once

#include <optional>

#include "horusrf/device/measurement_device.hpp"
#include "horusrf/device/rf_device.hpp"

namespace horusrf::device {

class SimulatedRfTester;
class SimulatedMeasurementDevice;

// Represents only the RF output shared by the two simulated instruments. Its
// state is deliberately inaccessible outside the simulator implementations.
class SimulatedRfConnection final {
private:
    std::optional<domain::Power> output_power_;

    friend class SimulatedRfTester;
    friend class SimulatedMeasurementDevice;
};

class SimulatedRfTester final : public RfDevice {
public:
    explicit SimulatedRfTester(SimulatedRfConnection& connection) noexcept;

    void setFrequency(domain::Frequency frequency) override;
    void setOutputPower(domain::Power power) override;

private:
    void update_output();

    SimulatedRfConnection& connection_;
    std::optional<domain::Frequency> frequency_;
    std::optional<domain::Power> requested_power_;
};

class SimulatedMeasurementDevice final : public MeasurementDevice {
public:
    explicit SimulatedMeasurementDevice(
        const SimulatedRfConnection& connection) noexcept;

    [[nodiscard]] domain::Power measurePower() override;

private:
    const SimulatedRfConnection& connection_;
};

} // namespace horusrf::device
