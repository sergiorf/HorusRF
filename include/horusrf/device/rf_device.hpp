#pragma once

#include "horusrf/domain/quantity.hpp"

namespace horusrf::device {

// TX-control contract for the RF tester and path under characterization.
class RfDevice {
public:
    virtual ~RfDevice() = default;

    virtual void setFrequency(domain::Frequency frequency) = 0;
    virtual void setOutputPower(domain::Power power) = 0;
};

} // namespace horusrf::device
