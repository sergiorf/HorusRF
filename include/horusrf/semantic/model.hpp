#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "horusrf/domain/units.hpp"
#include "horusrf/parser/token.hpp"

namespace horusrf::semantic {

enum class SemanticType { Frequency, Power, PowerDelta };

[[nodiscard]] constexpr const char* type_name(SemanticType type) noexcept {
    switch (type) {
    case SemanticType::Frequency: return "Frequency";
    case SemanticType::Power: return "Power";
    case SemanticType::PowerDelta: return "PowerDelta";
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

using ExpressionValue =
    std::variant<QuantityValue, SymbolValue, ReferenceValue, UnaryValue, BinaryValue>;

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
    SourceSpan span;
};

struct PowerMeasurement {
    SymbolId symbol;
    SourceSpan span;
};

struct DerivedQuantity {
    SymbolId symbol;
    std::string name;
    Expression expression;
    SemanticType type;
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
    PowerMeasurement measurement;
    std::vector<DerivedQuantity> derived_quantities;
    std::vector<Calibration> calibrations;
};

} // namespace horusrf::semantic
