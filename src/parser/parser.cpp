#include "horusrf/parser/parser.hpp"

#include <memory>
#include <utility>

#include "horusrf/parser/lexer.hpp"

namespace horusrf::parser {
namespace {

SourceSpan joined(SourcePosition begin, SourcePosition end) { return SourceSpan{begin, end}; }

} // namespace

ParseError::ParseError(Diagnostic diagnostic)
    : std::runtime_error(diagnostic.message), diagnostic_(std::move(diagnostic)) {}

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {
    if (tokens_.empty() || tokens_.back().kind != TokenKind::EndOfFile) {
        const SourcePosition end = tokens_.empty() ? SourcePosition{} : tokens_.back().span.end;
        tokens_.push_back(Token{TokenKind::EndOfFile, {}, SourceSpan{end, end}});
    }
}

const Token& Parser::current() const noexcept { return tokens_[index_]; }
const Token& Parser::previous() const noexcept { return tokens_[index_ - 1]; }
bool Parser::check(TokenKind kind) const noexcept { return current().kind == kind; }
bool Parser::match(TokenKind kind) noexcept {
    if (!check(kind)) return false;
    ++index_;
    return true;
}

void Parser::reject_invalid() const {
    if (check(TokenKind::Invalid)) {
        fail(DiagnosticCode::InvalidToken, "invalid token '" + current().lexeme + "'");
    }
}

const Token& Parser::consume(TokenKind kind, DiagnosticCode code, std::string message) {
    reject_invalid();
    if (check(kind)) {
        ++index_;
        return previous();
    }
    if (check(TokenKind::EndOfFile) && code == DiagnosticCode::UnexpectedToken) {
        fail(DiagnosticCode::UnexpectedEndOfFile, "unexpected end of file; " + message);
    }
    fail(code, std::move(message));
}

const Token& Parser::consume_expression_name(std::string message) {
    reject_invalid();
    // These words introduce statement syntax, but the grammar also uses them
    // as names in expressions, references, and calibration indexes.
    if (check(TokenKind::Identifier) || check(TokenKind::Power) ||
        check(TokenKind::Frequency)) {
        ++index_;
        return previous();
    }
    fail(DiagnosticCode::ExpectedIdentifier, std::move(message));
}

void Parser::fail(DiagnosticCode code, std::string message) const {
    throw ParseError(Diagnostic{code, std::move(message), current().span});
}

ast::Program Parser::parse_program() {
    reject_invalid();
    auto characterization = parse_characterization();
    consume(TokenKind::EndOfFile, DiagnosticCode::UnexpectedToken,
            "expected end of file after characterization");
    return ast::Program{characterization, characterization.span};
}

ast::Characterization Parser::parse_characterization() {
    const auto& start = consume(TokenKind::Characterize, DiagnosticCode::UnexpectedToken,
                                "expected 'characterize'");
    const auto& name = consume(TokenKind::Identifier, DiagnosticCode::ExpectedIdentifier,
                               "expected characterization name");
    consume(TokenKind::LeftBrace, DiagnosticCode::UnexpectedToken,
            "expected '{' after characterization name");
    std::vector<ast::Statement> statements;
    while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile)) {
        statements.push_back(parse_statement());
    }
    const auto& end = consume(TokenKind::RightBrace, DiagnosticCode::UnexpectedToken,
                              "expected '}' to close characterization");
    return ast::Characterization{name.lexeme, std::move(statements),
                                 joined(start.span.begin, end.span.end)};
}

ast::Statement Parser::parse_statement() {
    reject_invalid();
    switch (current().kind) {
    case TokenKind::Reference: return parse_reference_statement();
    case TokenKind::Sweep: return parse_sweep_statement();
    case TokenKind::Measure: return parse_measurement_statement();
    case TokenKind::Derive:
        if (index_ + 1 < tokens_.size() && tokens_[index_ + 1].kind == TokenKind::Calibration)
            return parse_calibration_statement();
        return parse_derive_statement();
    default:
        fail(DiagnosticCode::UnexpectedToken,
             "expected reference, sweep, measure, or derive statement");
    }
}

ast::ReferenceStatement Parser::parse_reference_statement() {
    const auto& start = consume(TokenKind::Reference, DiagnosticCode::UnexpectedToken,
                                "expected 'reference'");
    consume(TokenKind::Power, DiagnosticCode::UnexpectedToken,
            "expected 'power' after 'reference'");
    consume(TokenKind::Equal, DiagnosticCode::UnexpectedToken,
            "expected '=' after 'reference power'");
    auto quantity = parse_quantity(true);
    return ast::ReferenceStatement{quantity, joined(start.span.begin, quantity.span.end)};
}

ast::SweepStatement Parser::parse_sweep_statement() {
    const auto& start = consume(TokenKind::Sweep, DiagnosticCode::UnexpectedToken,
                                "expected 'sweep'");
    consume(TokenKind::Frequency, DiagnosticCode::UnexpectedToken,
            "expected 'frequency' after 'sweep'");
    auto range_start = parse_quantity();
    consume(TokenKind::Range, DiagnosticCode::UnexpectedToken,
            "expected '..' between sweep endpoints");
    auto range_end = parse_quantity();
    consume(TokenKind::Step, DiagnosticCode::UnexpectedToken,
            "expected 'step' after sweep range");
    auto step = parse_quantity();
    return ast::SweepStatement{range_start, range_end, step,
                               joined(start.span.begin, step.span.end)};
}

ast::MeasurementStatement Parser::parse_measurement_statement() {
    const auto& start = consume(TokenKind::Measure, DiagnosticCode::UnexpectedToken,
                                "expected 'measure'");
    const auto& end = consume(TokenKind::Power, DiagnosticCode::UnexpectedToken,
                              "expected 'power' after 'measure'");
    return ast::MeasurementStatement{joined(start.span.begin, end.span.end)};
}

ast::DeriveStatement Parser::parse_derive_statement() {
    const auto& start = consume(TokenKind::Derive, DiagnosticCode::UnexpectedToken,
                                "expected 'derive'");
    const auto& name = consume(TokenKind::Identifier, DiagnosticCode::ExpectedIdentifier,
                               "expected derived value name");
    consume(TokenKind::Equal, DiagnosticCode::UnexpectedToken,
            "expected '=' after derived value name");
    auto expression = parse_expression();
    return ast::DeriveStatement{name.lexeme, expression,
                                joined(start.span.begin, expression.span.end)};
}

ast::CalibrationStatement Parser::parse_calibration_statement() {
    const auto& start = consume(TokenKind::Derive, DiagnosticCode::UnexpectedToken,
                                "expected 'derive'");
    consume(TokenKind::Calibration, DiagnosticCode::UnexpectedToken,
            "expected 'calibration'");
    const auto& name = consume(TokenKind::Identifier, DiagnosticCode::ExpectedIdentifier,
                               "expected calibration name");
    consume(TokenKind::LeftBrace, DiagnosticCode::UnexpectedToken,
            "expected '{' after calibration name");
    consume(TokenKind::Correction, DiagnosticCode::UnexpectedToken,
            "expected 'correction' in calibration");
    consume(TokenKind::Equal, DiagnosticCode::UnexpectedToken,
            "expected '=' after 'correction'");
    auto correction = parse_expression();
    consume(TokenKind::Over, DiagnosticCode::UnexpectedToken,
            "expected 'over' after calibration correction");
    std::vector<std::string> dimensions;
    dimensions.push_back(consume_expression_name("expected dimension after 'over'").lexeme);
    while (match(TokenKind::Comma)) {
        dimensions.push_back(consume_expression_name("expected dimension after ','").lexeme);
    }
    const auto& end = consume(TokenKind::RightBrace, DiagnosticCode::UnexpectedToken,
                              "expected '}' to close calibration");
    return ast::CalibrationStatement{name.lexeme, correction, std::move(dimensions),
                                     joined(start.span.begin, end.span.end)};
}

ast::QuantityLiteral Parser::parse_quantity(bool allow_sign) {
    reject_invalid();
    SourcePosition begin = current().span.begin;
    std::string sign;
    if (allow_sign && match(TokenKind::Minus)) {
        sign = "-";
    }
    if (!check(TokenKind::Number)) {
        fail(DiagnosticCode::ExpectedQuantity, "expected quantity number");
    }
    const auto number = current();
    ++index_;
    reject_invalid();
    if (!is_unit(current().kind)) {
        fail(DiagnosticCode::ExpectedUnit, "expected unit after quantity number");
    }
    const auto unit = current();
    ++index_;
    return ast::QuantityLiteral{sign + number.lexeme, unit.lexeme,
                                joined(begin, unit.span.end)};
}

ast::Expression Parser::parse_expression() { return parse_additive(); }

ast::Expression Parser::parse_additive() {
    auto expression = parse_unary();
    while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
        const auto op = current().kind;
        ++index_;
        auto right = parse_unary();
        const auto span = joined(expression.span.begin, right.span.end);
        ast::BinaryExpression binary{
            op == TokenKind::Plus ? ast::BinaryOperator::Add : ast::BinaryOperator::Subtract,
            std::make_shared<ast::Expression>(std::move(expression)),
            std::make_shared<ast::Expression>(std::move(right)), span};
        expression = ast::Expression{std::move(binary), span};
    }
    return expression;
}

ast::Expression Parser::parse_unary() {
    reject_invalid();
    if (match(TokenKind::Minus)) {
        const auto begin = previous().span.begin;
        auto operand = parse_primary();
        const auto span = joined(begin, operand.span.end);
        return ast::Expression{ast::UnaryExpression{ast::UnaryOperator::Negate,
            std::make_shared<ast::Expression>(std::move(operand)), span}, span};
    }
    return parse_primary();
}

ast::Expression Parser::parse_primary() {
    reject_invalid();
    if (check(TokenKind::Number)) {
        auto quantity = parse_quantity();
        return ast::Expression{quantity, quantity.span};
    }
    if (match(TokenKind::Reference)) {
        const auto start = previous().span.begin;
        consume(TokenKind::Dot, DiagnosticCode::UnexpectedToken,
                "expected '.' after 'reference'");
        const auto& member = consume_expression_name("expected reference member after '.'");
        const auto span = joined(start, member.span.end);
        return ast::Expression{ast::ReferenceExpression{"reference", member.lexeme, span}, span};
    }
    if (check(TokenKind::Identifier) || check(TokenKind::Power) ||
        check(TokenKind::Frequency)) {
        const auto& identifier = current();
        ++index_;
        return ast::Expression{ast::IdentifierExpression{identifier.lexeme, identifier.span},
                               identifier.span};
    }
    if (match(TokenKind::LeftParen)) {
        const auto begin = previous().span.begin;
        auto expression = parse_expression();
        const auto& end = consume(TokenKind::RightParen, DiagnosticCode::UnexpectedToken,
                                  "expected ')' after expression");
        expression.span = joined(begin, end.span.end);
        return expression;
    }
    fail(DiagnosticCode::ExpectedExpression, "expected expression");
}

ast::Program parse(std::string_view source) { return Parser(lex(source)).parse_program(); }

} // namespace horusrf::parser
