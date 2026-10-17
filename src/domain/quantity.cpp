#include "horusrf/domain/quantity.hpp"

#include <cmath>
#include <stdexcept>

namespace horusrf::domain {
namespace {
void require_finite(double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument("RF quantity must be finite");
    }
}
} // namespace

Frequency Frequency::from_hertz(double value) {
    require_finite(value);
    return Frequency(value);
}

FrequencyDelta FrequencyDelta::from_hertz(double value) {
    require_finite(value);
    return FrequencyDelta(value);
}

Ratio Ratio::from_ratio(double value) {
    require_finite(value);
    return Ratio(value);
}

Temperature Temperature::from_kelvin(double value) {
    require_finite(value);
    if (value < 0.0) throw std::invalid_argument("absolute temperature must not be below 0 K");
    return Temperature(value);
}

TemperatureDelta TemperatureDelta::from_kelvin(double value) {
    require_finite(value);
    return TemperatureDelta(value);
}

Power Power::from_dbm(double value) {
    require_finite(value);
    return Power(value);
}

PowerDelta PowerDelta::from_db(double value) {
    require_finite(value);
    return PowerDelta(value);
}

PowerDelta operator-(Power lhs, Power rhs) {
    return PowerDelta::from_db(lhs.dbm() - rhs.dbm());
}

Power operator+(Power lhs, PowerDelta rhs) {
    return Power::from_dbm(lhs.dbm() + rhs.db());
}

PowerDelta operator+(PowerDelta lhs, PowerDelta rhs) {
    return PowerDelta::from_db(lhs.db() + rhs.db());
}

PowerDelta operator-(PowerDelta value) { return PowerDelta::from_db(-value.db()); }

TemperatureDelta operator-(Temperature lhs, Temperature rhs) {
    return TemperatureDelta::from_kelvin(lhs.kelvin() - rhs.kelvin());
}

Temperature operator+(Temperature lhs, TemperatureDelta rhs) {
    return Temperature::from_kelvin(lhs.kelvin() + rhs.kelvin());
}

Temperature operator-(Temperature lhs, TemperatureDelta rhs) {
    return Temperature::from_kelvin(lhs.kelvin() - rhs.kelvin());
}

TemperatureDelta operator+(TemperatureDelta lhs, TemperatureDelta rhs) {
    return TemperatureDelta::from_kelvin(lhs.kelvin() + rhs.kelvin());
}

TemperatureDelta operator-(TemperatureDelta lhs, TemperatureDelta rhs) {
    return TemperatureDelta::from_kelvin(lhs.kelvin() - rhs.kelvin());
}

TemperatureDelta operator-(TemperatureDelta value) {
    return TemperatureDelta::from_kelvin(-value.kelvin());
}

FrequencyDelta operator+(FrequencyDelta lhs, FrequencyDelta rhs) {
    return FrequencyDelta::from_hertz(lhs.hertz() + rhs.hertz());
}

FrequencyDelta operator-(FrequencyDelta lhs, FrequencyDelta rhs) {
    return FrequencyDelta::from_hertz(lhs.hertz() - rhs.hertz());
}

FrequencyDelta operator-(FrequencyDelta value) {
    return FrequencyDelta::from_hertz(-value.hertz());
}

Ratio operator+(Ratio lhs, Ratio rhs) {
    return Ratio::from_ratio(lhs.ratio() + rhs.ratio());
}

Ratio operator-(Ratio lhs, Ratio rhs) {
    return Ratio::from_ratio(lhs.ratio() - rhs.ratio());
}

Ratio operator-(Ratio value) { return Ratio::from_ratio(-value.ratio()); }

} // namespace horusrf::domain
