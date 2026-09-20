#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "horusrf/domain/quantity.hpp"
#include "horusrf/parser/token.hpp"

namespace horusrf::ir {

enum class ValueType { Frequency, Power, PowerDelta };

[[nodiscard]] constexpr std::string_view type_name(ValueType type) noexcept {
    switch (type) {
    case ValueType::Frequency: return "Frequency";
    case ValueType::Power: return "Power";
    case ValueType::PowerDelta: return "PowerDelta";
    }
    return "unknown";
}

struct ValueId {
    std::size_t value{};
    friend bool operator==(ValueId, ValueId) = default;
};

using ConstantValue =
    std::variant<domain::Frequency, domain::Power, domain::PowerDelta>;

struct Constant {
    ConstantValue value;
    friend bool operator==(const Constant&, const Constant&) = default;
};
struct ReferencePower {
    domain::Power value;
    friend bool operator==(const ReferencePower&, const ReferencePower&) = default;
};
struct SweepFrequency {
    friend bool operator==(SweepFrequency, SweepFrequency) = default;
};
struct MeasurePower {
    friend bool operator==(MeasurePower, MeasurePower) = default;
};
struct Alias {
    ValueId value;
    friend bool operator==(Alias, Alias) = default;
};
struct NegatePowerDelta {
    ValueId operand;
    friend bool operator==(NegatePowerDelta, NegatePowerDelta) = default;
};
struct AddPowerDelta {
    ValueId left;
    ValueId right;
    friend bool operator==(AddPowerDelta, AddPowerDelta) = default;
};
struct ApplyPowerDelta {
    ValueId power;
    ValueId delta;
    friend bool operator==(ApplyPowerDelta, ApplyPowerDelta) = default;
};
struct PowerDifference {
    ValueId left;
    ValueId right;
    friend bool operator==(PowerDifference, PowerDifference) = default;
};

using Definition = std::variant<Constant, ReferencePower, SweepFrequency, MeasurePower,
                                Alias, NegatePowerDelta, AddPowerDelta,
                                ApplyPowerDelta, PowerDifference>;

struct Value {
    ValueId id;
    ValueType type;
    Definition definition;
    parser::SourceSpan span;
    friend bool operator==(const Value&, const Value&) = default;
};

struct Sweep {
    ValueId frequency;
    domain::Frequency start;
    domain::Frequency end;
    domain::Frequency step;
    parser::SourceSpan span;
    friend bool operator==(const Sweep&, const Sweep&) = default;
};

struct NamedValue {
    std::string name;
    ValueId value;
    ValueType type;
    parser::SourceSpan span;
    friend bool operator==(const NamedValue&, const NamedValue&) = default;
};

struct Calibration {
    std::string name;
    ValueId correction;
    std::vector<ValueId> indexes;
    std::vector<ValueType> index_types;
    ValueType correction_type{ValueType::PowerDelta};
    parser::SourceSpan span;
    friend bool operator==(const Calibration&, const Calibration&) = default;
};

struct Program {
    std::string characterization_name;
    parser::SourceSpan span;
    ValueId reference;
    Sweep sweep;
    ValueId measurement;
    std::vector<Value> values;
    std::vector<NamedValue> named_values;
    std::vector<Calibration> calibrations;
    friend bool operator==(const Program&, const Program&) = default;
};

} // namespace horusrf::ir
