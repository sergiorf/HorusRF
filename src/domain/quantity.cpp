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

} // namespace horusrf::domain
