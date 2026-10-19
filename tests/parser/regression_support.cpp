#include "regression_support.hpp"

#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <variant>

#include "horusrf/parser/lexer.hpp"

namespace horusrf::test::parser_regression {
namespace {

std::string position_text(const parser::SourcePosition& position) {
    return std::to_string(position.line) + ':' + std::to_string(position.column) + '@' +
           std::to_string(position.offset);
}

std::string span_text(const parser::SourceSpan& span) {
    return '[' + position_text(span.begin) + '-' + position_text(span.end) + ']';
}

std::string unary_operator_name(ast::UnaryOperator op) {
    switch (op) {
    case ast::UnaryOperator::Negate: return "negate";
    }
    return "unknown";
}

std::string binary_operator_name(ast::BinaryOperator op) {
    switch (op) {
    case ast::BinaryOperator::Add: return "add";
    case ast::BinaryOperator::Subtract: return "subtract";
    }
    return "unknown";
}

std::string comparison_operator_name(ast::ComparisonOperator op) {
    switch (op) {
    case ast::ComparisonOperator::Less: return "less";
    case ast::ComparisonOperator::LessEqual: return "less_equal";
    case ast::ComparisonOperator::Greater: return "greater";
    case ast::ComparisonOperator::GreaterEqual: return "greater_equal";
    case ast::ComparisonOperator::Equal: return "equal";
    case ast::ComparisonOperator::NotEqual: return "not_equal";
    }
    return "unknown";
}

void append_expression(std::ostringstream& output, const ast::Expression& expression) {
    std::visit(
        [&](const auto& value) {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, ast::QuantityLiteral>) {
                output << "quantity(" << value.number << ',' << value.unit << ')';
            } else if constexpr (std::is_same_v<Value, ast::IdentifierExpression>) {
                output << "identifier(" << value.name << ')';
            } else if constexpr (std::is_same_v<Value, ast::ReferenceExpression>) {
                output << "reference(" << value.object << '.' << value.member << ')';
            } else if constexpr (std::is_same_v<Value, ast::UnaryExpression>) {
                output << "unary(" << unary_operator_name(value.op) << ',';
                append_expression(output, *value.operand);
                output << ')';
            } else if constexpr (std::is_same_v<Value, ast::BinaryExpression>) {
                output << "binary(" << binary_operator_name(value.op) << ',';
                append_expression(output, *value.left);
                output << ',';
                append_expression(output, *value.right);
                output << ')';
            } else if constexpr (std::is_same_v<Value, ast::CallExpression>) {
                output << "call(" << value.function << ',';
                append_expression(output, *value.argument);
                output << ')';
            } else if constexpr (std::is_same_v<Value, ast::ComparisonExpression>) {
                output << "comparison(" << comparison_operator_name(value.op) << ',';
                append_expression(output, *value.left);
                output << ',';
                append_expression(output, *value.right);
                output << ')';
            }
            output << span_text(value.span);
        },
        expression.value);
    output << " expression" << span_text(expression.span);
}

void append_quantity(std::ostringstream& output, const ast::QuantityLiteral& quantity) {
    output << quantity.number << ' ' << quantity.unit << span_text(quantity.span);
}

void append_statement(std::ostringstream& output, const ast::Statement& statement) {
    std::visit(
        [&](const auto& value) {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, ast::ReferenceStatement>) {
                output << "reference power=";
                append_quantity(output, value.power);
            } else if constexpr (std::is_same_v<Value, ast::SweepStatement>) {
                output << "sweep " << value.dimension << " start=";
                append_quantity(output, value.start);
                output << " end=";
                append_quantity(output, value.end);
                output << " step=";
                append_quantity(output, value.step);
            } else if constexpr (std::is_same_v<Value, ast::MeasurementStatement>) {
                output << "measurement name=" << value.name << " operation=" << value.operation
                       << " legacy=" << (value.legacy ? "true" : "false");
            } else if constexpr (std::is_same_v<Value, ast::DeriveStatement>) {
                output << "derive name=" << value.name << " value=";
                append_expression(output, value.expression);
            } else if constexpr (std::is_same_v<Value, ast::AssertionStatement>) {
                output << "assert value=";
                append_expression(output, value.expression);
            } else if constexpr (std::is_same_v<Value, ast::CalibrationStatement>) {
                output << "calibration name=" << value.name << " correction=";
                append_expression(output, value.correction);
                output << " dimensions=";
                for (std::size_t index = 0; index < value.dimensions.size(); ++index) {
                    if (index != 0) output << ',';
                    output << value.dimensions[index];
                }
            }
            output << " statement" << span_text(value.span);
        },
        statement);
}

} // namespace

Frontend handwritten_frontend() {
    return Frontend{"handwritten", parser::lex, parser::parse};
}

std::string token_snapshot(const std::vector<parser::Token>& tokens) {
    std::ostringstream output;
    for (const auto& token : tokens) {
        output << parser::token_kind_name(token.kind) << "|" << token.lexeme << "|"
               << span_text(token.span) << '\n';
    }
    return output.str();
}

std::string ast_snapshot(const ast::Program& program) {
    std::ostringstream output;
    output << "program" << span_text(program.span) << '\n';
    output << "characterization name=" << program.characterization.name
           << span_text(program.characterization.span) << '\n';
    for (std::size_t index = 0; index < program.characterization.statements.size(); ++index) {
        output << index << ':';
        append_statement(output, program.characterization.statements[index]);
        output << '\n';
    }
    return output.str();
}

std::string diagnostic_snapshot(const parser::Diagnostic& diagnostic) {
    return std::string{parser::diagnostic_code_name(diagnostic.code)} + '|' +
           span_text(diagnostic.span);
}

Observation observe(const Frontend& frontend, std::string_view source) {
    try {
        const auto tokens = frontend.tokenize(source);
        const auto program = frontend.parse(source);
        return Observation{true, token_snapshot(tokens) + ast_snapshot(program)};
    } catch (const parser::ParseError& error) {
        return Observation{false, diagnostic_snapshot(error.diagnostic())};
    }
}

void require_equivalent(const Frontend& expected, const Frontend& actual,
                        std::string_view case_name, std::string_view source) {
    const auto expected_observation = observe(expected, source);
    const auto actual_observation = observe(actual, source);
    if (expected_observation == actual_observation) return;

    std::ostringstream message;
    message << "frontend mismatch for " << case_name << " (" << expected.name << " vs "
            << actual.name << ")\nexpected:\n" << expected_observation.snapshot
            << "\nactual:\n" << actual_observation.snapshot;
    throw std::runtime_error(message.str());
}

} // namespace horusrf::test::parser_regression
