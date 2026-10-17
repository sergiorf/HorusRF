#pragma once

#include "horusrf/domain/quantity.hpp"

namespace horusrf::device {

class TemperatureController {
public:
    virtual ~TemperatureController() = default;
    virtual void setTemperature(domain::Temperature temperature) = 0;
};

} // namespace horusrf::device
