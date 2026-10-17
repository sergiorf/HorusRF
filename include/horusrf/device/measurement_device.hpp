#pragma once

#include "horusrf/domain/quantity.hpp"

namespace horusrf::device {

class MeasurementDevice {
public:
    virtual ~MeasurementDevice() = default;

    [[nodiscard]] virtual domain::Power measurePower() = 0;
};

class EvmMeasurementDevice {
public:
    virtual ~EvmMeasurementDevice() = default;
    [[nodiscard]] virtual domain::Ratio measureEvm() = 0;
};

class FrequencyErrorMeasurementDevice {
public:
    virtual ~FrequencyErrorMeasurementDevice() = default;
    [[nodiscard]] virtual domain::FrequencyDelta measureFrequencyError() = 0;
};

} // namespace horusrf::device
