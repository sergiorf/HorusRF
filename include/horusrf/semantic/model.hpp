#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "horusrf/domain/units.hpp"
#include "horusrf/parser/token.hpp"

namespace horusrf::semantic {

enum class SemanticType { Frequency, FrequencyDelta, Ratio, Temperature, TemperatureDelta, Power, PowerDelta, Boolean };

[[nodiscard]] constexpr const char* type_name(SemanticType type) noexcept {
    switch (type) {
    case SemanticType::Frequency: return "Frequency";
    case SemanticType::FrequencyDelta: return "FrequencyDelta";
    case SemanticType::Ratio: return "Ratio";
    case SemanticType::Temperature: return "Temperature";
    case SemanticType::TemperatureDelta: return "TemperatureDelta";
    case SemanticType::Power: return "Power";
    case SemanticType::PowerDelta: return "PowerDelta";
    case SemanticType::Boolean: return "Boolean";
    }
    return "unknown";
}

struct SymbolId {
    std::size_t value{};
    friend bool operator==(SymbolId, SymbolId) = default;
};

using parser::SourceSpan;

struct QuantityValue {
    domain::CanonicalQuantity value;
    SourceSpan span;
};

struct SymbolValue {
    SymbolId symbol;
    std::string name;
    SemanticType type;
    SourceSpan span;
};

struct ReferenceValue {
    SymbolId symbol;
    std::string object;
    std::string member;
    SemanticType type;
    SourceSpan span;
};

enum class UnaryOperator { Negate };
enum class BinaryOperator { Add, Subtract };
struct Expression;

struct UnaryValue {
    UnaryOperator op{UnaryOperator::Negate};
    std::shared_ptr<Expression> operand;
    SemanticType type;
    SourceSpan span;
};

struct BinaryValue {
    BinaryOperator op{BinaryOperator::Add};
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
    SemanticType type;
    SourceSpan span;
};

struct CallValue {
    std::string function;
    std::shared_ptr<Expression> argument;
    SemanticType type;
    SourceSpan span;
};

enum class ComparisonOperator { Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual };
struct ComparisonValue {
    ComparisonOperator op{ComparisonOperator::Equal};
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
    SourceSpan span;
};

using ExpressionValue =
    std::variant<QuantityValue, SymbolValue, ReferenceValue, UnaryValue, BinaryValue,
                 CallValue, ComparisonValue>;

struct Expression {
    ExpressionValue value;
    SemanticType type;
    SourceSpan span;
};

struct ReferencePower {
    SymbolId symbol;
    domain::Power value;
    SourceSpan span;
};

struct FrequencySweep {
    SymbolId symbol;
    domain::Frequency start;
    domain::Frequency end;
    domain::Frequency step;
    std::size_t point_count{};
    SourceSpan span;
};

struct TemperatureSweep {
    SymbolId symbol;
    domain::Temperature start;
    domain::Temperature end;
    domain::TemperatureDelta step;
    std::size_t point_count{};
    SourceSpan span;
};

using Sweep = std::variant<FrequencySweep, TemperatureSweep>;

struct PowerMeasurement {
    SymbolId symbol;
    SourceSpan span;
};

enum class MeasurementOperation { Power, Evm, FrequencyError };

struct Measurement {
    SymbolId symbol;
    std::string name;
    MeasurementOperation operation{MeasurementOperation::Power};
    SemanticType type{SemanticType::Power};
    bool legacy{};
    SourceSpan span;
};

struct DerivedQuantity {
    SymbolId symbol;
    std::string name;
    Expression expression;
    SemanticType type;
    SourceSpan span;
};

struct Assertion {
    std::string id;
    std::size_t ordinal{};
    Expression expression;
    SourceSpan span;
};

struct CalibrationType {
    std::vector<SemanticType> index_types;
    SemanticType correction_type{SemanticType::PowerDelta};
};

struct Calibration {
    SymbolId symbol;
    std::string name;
    Expression correction;
    std::vector<SymbolId> indexes;
    CalibrationType type;
    SourceSpan span;
};

struct AnalyzedProgram {
    std::string characterization_name;
    SourceSpan span;
    ReferencePower reference;
    FrequencySweep sweep;
    std::vector<Sweep> sweeps;
    std::size_t total_point_count{};
    PowerMeasurement measurement;
    std::vector<Measurement> measurements;
    std::vector<DerivedQuantity> derived_quantities;
    std::vector<Assertion> assertions;
    Calibration calibration;
};

} // namespace horusrf::semantic
