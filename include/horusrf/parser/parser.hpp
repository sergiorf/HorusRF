#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "horusrf/ast/ast.hpp"
#include "horusrf/parser/token.hpp"

namespace horusrf::parser {

enum class DiagnosticCode {
    UnexpectedToken,
    UnexpectedEndOfFile,
    InvalidToken,
    ExpectedIdentifier,
    ExpectedQuantity,
    ExpectedUnit,
    ExpectedExpression
};

[[nodiscard]] std::string_view diagnostic_code_name(DiagnosticCode code) noexcept;

struct Diagnostic {
    DiagnosticCode code{DiagnosticCode::UnexpectedToken};
    std::string message;
    SourceSpan span;
};

class ParseError : public std::runtime_error {
public:
    explicit ParseError(Diagnostic diagnostic);
    [[nodiscard]] const Diagnostic& diagnostic() const noexcept { return diagnostic_; }

private:
    Diagnostic diagnostic_;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    [[nodiscard]] ast::Program parse_program();

private:
    [[nodiscard]] const Token& current() const noexcept;
    [[nodiscard]] const Token& previous() const noexcept;
    [[nodiscard]] bool check(TokenKind kind) const noexcept;
    bool match(TokenKind kind) noexcept;
    const Token& consume(TokenKind kind, DiagnosticCode code, std::string message);
    const Token& consume_expression_name(std::string message);
    [[noreturn]] void fail(DiagnosticCode code, std::string message) const;
    void reject_invalid() const;

    [[nodiscard]] ast::Characterization parse_characterization();
    [[nodiscard]] ast::Statement parse_statement();
    [[nodiscard]] ast::ReferenceStatement parse_reference_statement();
    [[nodiscard]] ast::SweepStatement parse_sweep_statement();
    [[nodiscard]] ast::MeasurementStatement parse_measurement_statement();
    [[nodiscard]] ast::DeriveStatement parse_derive_statement();
    [[nodiscard]] ast::CalibrationStatement parse_calibration_statement();
    [[nodiscard]] ast::QuantityLiteral parse_quantity(bool allow_sign = false);
    [[nodiscard]] ast::Expression parse_expression();
    [[nodiscard]] ast::Expression parse_additive();
    [[nodiscard]] ast::Expression parse_unary();
    [[nodiscard]] ast::Expression parse_primary();

    std::vector<Token> tokens_;
    std::size_t index_{0};
};

[[nodiscard]] ast::Program parse(std::string_view source);

} // namespace horusrf::parser
