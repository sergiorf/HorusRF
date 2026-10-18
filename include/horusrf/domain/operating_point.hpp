#pragma once

#include <string>
#include <variant>
#include <vector>

#include "horusrf/domain/quantity.hpp"

namespace horusrf::domain {

enum class CoordinateType { Frequency, Temperature };
using CoordinateValue = std::variant<Frequency, Temperature>;

struct Coordinate {
    std::string name;
    CoordinateType type{CoordinateType::Frequency};
    CoordinateValue value;
    std::string display_unit;
    friend bool operator==(const Coordinate&, const Coordinate&) = default;
};

struct OperatingPoint {
    std::vector<Coordinate> coordinates;
    friend bool operator==(const OperatingPoint&, const OperatingPoint&) = default;
};

} // namespace horusrf::domain
