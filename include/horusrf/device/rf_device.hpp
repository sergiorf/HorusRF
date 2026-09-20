#pragma once

#include "horusrf/domain/quantity.hpp"

namespace horusrf::device {

class RfDevice {
public:
    virtual ~RfDevice() = default;

    virtual void setFrequency(domain::Frequency frequency) = 0;
    virtual void setOutputPower(domain::Power power) = 0;
    [[nodiscard]] virtual domain::Power measurePower() = 0;
};

} // namespace horusrf::device
