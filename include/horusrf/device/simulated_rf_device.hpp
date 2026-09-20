#pragma once

#include <optional>

#include "horusrf/device/rf_device.hpp"

namespace horusrf::device {

class SimulatedRfDevice final : public RfDevice {
public:
    void setFrequency(domain::Frequency frequency) override;
    void setOutputPower(domain::Power power) override;
    [[nodiscard]] domain::Power measurePower() override;

private:
    std::optional<domain::Frequency> frequency_;
    std::optional<domain::Power> output_power_;
};

} // namespace horusrf::device
