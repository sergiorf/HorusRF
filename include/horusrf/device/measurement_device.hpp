#pragma once

#include "horusrf/domain/quantity.hpp"

namespace horusrf::device {

class MeasurementDevice {
public:
    virtual ~MeasurementDevice() = default;

    [[nodiscard]] virtual domain::Power measurePower() = 0;
};

} // namespace horusrf::device
