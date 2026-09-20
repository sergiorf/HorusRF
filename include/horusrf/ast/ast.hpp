#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "horusrf/parser/token.hpp"

namespace horusrf::ast {

using parser::SourceSpan;

struct QuantityLiteral {
    std::string number;
    std::string unit;
    SourceSpan span;
};

struct IdentifierExpression {
    std::string name;
    SourceSpan span;
};

struct ReferenceExpression {
    std::string object;
    std::string member;
    SourceSpan span;
};

struct Expression;

enum class UnaryOperator { Negate };
struct UnaryExpression {
    UnaryOperator op{UnaryOperator::Negate};
    std::shared_ptr<Expression> operand;
    SourceSpan span;
};

enum class BinaryOperator { Add, Subtract };
struct BinaryExpression {
    BinaryOperator op{BinaryOperator::Add};
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
    SourceSpan span;
};

using ExpressionValue = std::variant<QuantityLiteral, IdentifierExpression,
                                     ReferenceExpression, UnaryExpression,
                                     BinaryExpression>;
struct Expression {
    ExpressionValue value;
    SourceSpan span;
};

struct ReferenceStatement {
    QuantityLiteral power;
    SourceSpan span;
};
struct SweepStatement {
    QuantityLiteral start;
    QuantityLiteral end;
    QuantityLiteral step;
    SourceSpan span;
};
struct MeasurementStatement { SourceSpan span; };
struct DeriveStatement {
    std::string name;
    Expression expression;
    SourceSpan span;
};
struct CalibrationStatement {
    std::string name;
    Expression correction;
    std::vector<std::string> dimensions;
    SourceSpan span;
};

using Statement = std::variant<ReferenceStatement, SweepStatement,
                               MeasurementStatement, DeriveStatement,
                               CalibrationStatement>;

struct Characterization {
    std::string name;
    std::vector<Statement> statements;
    SourceSpan span;
};
struct Program {
    Characterization characterization;
    SourceSpan span;
};

} // namespace horusrf::ast
