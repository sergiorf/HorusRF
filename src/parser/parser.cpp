#include "horusrf/parser/parser.hpp"

#include "HorusRfLexer.h"
#include "HorusRfParser.h"

#include <antlr4-runtime.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "horusrf/parser/lexer.hpp"

namespace horusrf::parser {
namespace {

class SourceMap {
public:
    explicit SourceMap(std::string_view source) {
        positions_.reserve(source.size() + 1);
        SourcePosition position{};
        for (const char byte : source) {
            positions_.push_back(position);
            ++position.offset;
            if (byte == '\n') {
                ++position.line;
                position.column = 1;
            } else {
                ++position.column;
            }
        }
        positions_.push_back(position);

        symbol_offsets_.push_back(0);
        for (std::size_t offset = 0; offset < source.size();) {
            const auto lead = static_cast<unsigned char>(source[offset]);
            std::size_t width = 1;
            if ((lead & 0xE0U) == 0xC0U) width = 2;
            else if ((lead & 0xF0U) == 0xE0U) width = 3;
            else if ((lead & 0xF8U) == 0xF0U) width = 4;
            if (offset + width > source.size()) width = 1;
            for (std::size_t index = 1; index < width; ++index) {
                if ((static_cast<unsigned char>(source[offset + index]) & 0xC0U) != 0x80U) {
                    width = 1;
                    break;
                }
            }
            offset += width;
            symbol_offsets_.push_back(offset);
        }
    }

    [[nodiscard]] SourcePosition position(std::size_t symbol_offset) const {
        const auto index = std::min(symbol_offset, symbol_offsets_.size() - 1);
        return positions_[symbol_offsets_[index]];
    }

    [[nodiscard]] SourceSpan span(const antlr4::Token* token) const {
        if (token->getType() == antlr4::Token::EOF) {
            const auto end = positions_.back();
            return {end, end};
        }
        const auto begin = position(token->getStartIndex());
        const auto end = position(token->getStopIndex() + 1);
        return {begin, end};
    }

    [[nodiscard]] SourceSpan span(const antlr4::ParserRuleContext* context) const {
        return {span(context->getStart()).begin, span(context->getStop()).end};
    }

private:
    std::vector<SourcePosition> positions_;
    std::vector<std::size_t> symbol_offsets_;
};

TokenKind token_kind(std::size_t type) {
    switch (type) {
    case antlr4::Token::EOF: return TokenKind::EndOfFile;
    case HorusRfLexer::IDENTIFIER: return TokenKind::Identifier;
    case HorusRfLexer::NUMBER: return TokenKind::Number;
    case HorusRfLexer::CHARACTERIZE: return TokenKind::Characterize;
    case HorusRfLexer::REFERENCE: return TokenKind::Reference;
    case HorusRfLexer::POWER: return TokenKind::Power;
    case HorusRfLexer::SWEEP: return TokenKind::Sweep;
    case HorusRfLexer::FREQUENCY: return TokenKind::Frequency;
    case HorusRfLexer::TEMPERATURE: return TokenKind::Temperature;
    case HorusRfLexer::STEP: return TokenKind::Step;
    case HorusRfLexer::MEASURE: return TokenKind::Measure;
    case HorusRfLexer::DERIVE: return TokenKind::Derive;
    case HorusRfLexer::CALIBRATION: return TokenKind::Calibration;
    case HorusRfLexer::CORRECTION: return TokenKind::Correction;
    case HorusRfLexer::OVER: return TokenKind::Over;
    case HorusRfLexer::ASSERT: return TokenKind::Assert;
    case HorusRfLexer::LEFT_BRACE: return TokenKind::LeftBrace;
    case HorusRfLexer::RIGHT_BRACE: return TokenKind::RightBrace;
    case HorusRfLexer::EQUAL: return TokenKind::Equal;
    case HorusRfLexer::EQUAL_EQUAL: return TokenKind::EqualEqual;
    case HorusRfLexer::NOT_EQUAL: return TokenKind::NotEqual;
    case HorusRfLexer::LESS: return TokenKind::Less;
    case HorusRfLexer::LESS_EQUAL: return TokenKind::LessEqual;
    case HorusRfLexer::GREATER: return TokenKind::Greater;
    case HorusRfLexer::GREATER_EQUAL: return TokenKind::GreaterEqual;
    case HorusRfLexer::RANGE: return TokenKind::Range;
    case HorusRfLexer::DOT: return TokenKind::Dot;
    case HorusRfLexer::PLUS: return TokenKind::Plus;
    case HorusRfLexer::MINUS: return TokenKind::Minus;
    case HorusRfLexer::LEFT_PAREN: return TokenKind::LeftParen;
    case HorusRfLexer::RIGHT_PAREN: return TokenKind::RightParen;
    case HorusRfLexer::COMMA: return TokenKind::Comma;
    case HorusRfLexer::UNIT_HZ: return TokenKind::UnitHz;
    case HorusRfLexer::UNIT_KHZ: return TokenKind::UnitKHz;
    case HorusRfLexer::UNIT_MHZ: return TokenKind::UnitMHz;
    case HorusRfLexer::UNIT_GHZ: return TokenKind::UnitGHz;
    case HorusRfLexer::UNIT_DBM: return TokenKind::UnitDBm;
    case HorusRfLexer::UNIT_DB: return TokenKind::UnitDB;
    case HorusRfLexer::UNIT_K: return TokenKind::UnitK;
    case HorusRfLexer::UNIT_DEGC: return TokenKind::UnitDegC;
    case HorusRfLexer::UNIT_DELTA_K: return TokenKind::UnitDeltaK;
    case HorusRfLexer::UNIT_DELTA_C: return TokenKind::UnitDeltaC;
    case HorusRfLexer::UNIT_PERCENT: return TokenKind::UnitPercent;
    case HorusRfLexer::MALFORMED_NUMBER:
    case HorusRfLexer::INVALID: return TokenKind::Invalid;
    default: throw std::logic_error("unmapped ANTLR token type");
    }
}

FixedTokenCategory fixed_token_category(TokenKind kind) {
    switch (kind) {
    case TokenKind::Characterize:
    case TokenKind::Reference:
    case TokenKind::Power:
    case TokenKind::Sweep:
    case TokenKind::Frequency:
    case TokenKind::Temperature:
    case TokenKind::Step:
    case TokenKind::Measure:
    case TokenKind::Derive:
    case TokenKind::Calibration:
    case TokenKind::Correction:
    case TokenKind::Over:
    case TokenKind::Assert: return FixedTokenCategory::Keyword;
    case TokenKind::UnitHz:
    case TokenKind::UnitKHz:
    case TokenKind::UnitMHz:
    case TokenKind::UnitGHz:
    case TokenKind::UnitDBm:
    case TokenKind::UnitDB:
    case TokenKind::UnitK:
    case TokenKind::UnitDegC:
    case TokenKind::UnitDeltaK:
    case TokenKind::UnitDeltaC:
    case TokenKind::UnitPercent: return FixedTokenCategory::Unit;
    case TokenKind::EqualEqual:
    case TokenKind::NotEqual:
    case TokenKind::Less:
    case TokenKind::LessEqual:
    case TokenKind::Greater:
    case TokenKind::GreaterEqual:
    case TokenKind::Plus:
    case TokenKind::Minus: return FixedTokenCategory::Operator;
    default: return FixedTokenCategory::Punctuation;
    }
}

std::string literal_spelling(const antlr4::dfa::Vocabulary& vocabulary,
                             std::size_t type) {
    auto literal = vocabulary.getLiteralName(type);
    if (literal.size() >= 2 && literal.front() == '\'' && literal.back() == '\'') {
        literal = literal.substr(1, literal.size() - 2);
    }
    return std::string{literal};
}

bool contains(const antlr4::misc::IntervalSet& tokens, std::size_t type) {
    return tokens.contains(type);
}

bool expects_only_names(const antlr4::misc::IntervalSet& expected) {
    bool found = false;
    for (const auto type : {HorusRfParser::IDENTIFIER, HorusRfParser::POWER,
                            HorusRfParser::FREQUENCY, HorusRfParser::TEMPERATURE}) {
        found = found || contains(expected, type);
    }
    if (!found) return false;
    for (const auto type : expected.toList()) {
        if (type != HorusRfParser::IDENTIFIER && type != HorusRfParser::POWER &&
            type != HorusRfParser::FREQUENCY && type != HorusRfParser::TEMPERATURE) {
            return false;
        }
    }
    return true;
}

bool expects_derive_name(const antlr4::misc::IntervalSet& expected) {
    if (!contains(expected, HorusRfParser::IDENTIFIER)) return false;
    for (const auto type : expected.toList()) {
        if (type != HorusRfParser::IDENTIFIER && type != HorusRfParser::CALIBRATION) {
            return false;
        }
    }
    return true;
}

bool expects_unit(const antlr4::misc::IntervalSet& expected) {
    for (std::size_t type = HorusRfParser::UNIT_HZ; type <= HorusRfParser::UNIT_PERCENT; ++type) {
        if (contains(expected, type)) return true;
    }
    return false;
}

bool expects_expression(const antlr4::misc::IntervalSet& expected) {
    for (const auto type : {HorusRfParser::MINUS, HorusRfParser::NUMBER,
                            HorusRfParser::REFERENCE, HorusRfParser::IDENTIFIER,
                            HorusRfParser::POWER, HorusRfParser::FREQUENCY,
                            HorusRfParser::TEMPERATURE, HorusRfParser::LEFT_PAREN}) {
        if (contains(expected, type)) return true;
    }
    return false;
}

class ErrorListener final : public antlr4::BaseErrorListener {
public:
    ErrorListener(const SourceMap& source_map, HorusRfParser& parser)
        : source_map_(source_map), parser_(parser) {}

    void syntaxError(antlr4::Recognizer*, antlr4::Token* offending_symbol,
                     std::size_t, std::size_t, const std::string&,
                     std::exception_ptr) override {
        const auto type = offending_symbol->getType();
        if (type == HorusRfParser::INVALID || type == HorusRfParser::MALFORMED_NUMBER) {
            throw ParseError({DiagnosticCode::InvalidToken,
                              "invalid token '" + offending_symbol->getText() + "'",
                              source_map_.span(offending_symbol)});
        }

        const auto expected = parser_.getExpectedTokens();
        const auto token_index = offending_symbol->getTokenIndex();
        const bool follows_derive =
            token_index > 0 &&
            parser_.getTokenStream()->get(token_index - 1)->getType() == HorusRfParser::DERIVE;
        const auto rule = parser_.getContext() == nullptr
                              ? HorusRfParser::RuleProgram
                              : parser_.getContext()->getRuleIndex();
        DiagnosticCode code = DiagnosticCode::UnexpectedToken;
        std::string message = "unexpected token '" + offending_symbol->getText() + "'";
        if (expects_only_names(expected) || expects_derive_name(expected) || follows_derive) {
            code = DiagnosticCode::ExpectedIdentifier;
            message = "expected identifier";
        } else if (rule == HorusRfParser::RuleUnit && expects_unit(expected)) {
            code = DiagnosticCode::ExpectedUnit;
            message = "expected unit after quantity number";
        } else if ((rule == HorusRfParser::RuleQuantity ||
                    rule == HorusRfParser::RuleSignedQuantity) &&
                   contains(expected, HorusRfParser::NUMBER)) {
            code = DiagnosticCode::ExpectedQuantity;
            message = "expected quantity number";
        } else if ((rule == HorusRfParser::RuleExpression ||
                    rule == HorusRfParser::RuleComparisonExpression ||
                    rule == HorusRfParser::RuleAdditiveExpression ||
                    rule == HorusRfParser::RuleUnaryExpression ||
                    rule == HorusRfParser::RulePrimaryExpression ||
                    rule == HorusRfParser::RuleCall) &&
                   expects_expression(expected)) {
            code = DiagnosticCode::ExpectedExpression;
            message = "expected expression";
        } else if (type == antlr4::Token::EOF) {
            code = DiagnosticCode::UnexpectedEndOfFile;
            message = "unexpected end of file";
        }
        throw ParseError({code, std::move(message), source_map_.span(offending_symbol)});
    }

private:
    const SourceMap& source_map_;
    HorusRfParser& parser_;
};

class FailFastErrorStrategy final : public antlr4::DefaultErrorStrategy {
public:
    void recover(antlr4::Parser* recognizer, std::exception_ptr error) override {
        recognizer->notifyErrorListeners(recognizer->getCurrentToken(), "syntax error", error);
        throw std::runtime_error("unreachable ANTLR recovery");
    }

    antlr4::Token* recoverInline(antlr4::Parser* recognizer) override {
        recognizer->notifyErrorListeners(recognizer->getCurrentToken(), "syntax error", nullptr);
        throw std::runtime_error("unreachable ANTLR inline recovery");
    }
};

class AstBuilder {
public:
    AstBuilder(const SourceMap& source_map, antlr4::CommonTokenStream& tokens)
        : source_map_(source_map), tokens_(tokens) {}

    ast::Program build(HorusRfParser::ProgramContext* context) {
        auto characterization = build_characterization(context->characterization());
        return {characterization, characterization.span};
    }

private:
    ast::Characterization build_characterization(HorusRfParser::CharacterizationContext* context) {
        std::vector<ast::Statement> statements;
        statements.reserve(context->statement().size());
        for (auto* statement : context->statement()) statements.push_back(build_statement(statement));
        return {context->IDENTIFIER()->getText(), std::move(statements), source_map_.span(context)};
    }

    ast::Statement build_statement(HorusRfParser::StatementContext* context) {
        if (auto* value = context->referenceStatement()) return build_reference_statement(value);
        if (auto* value = context->sweepStatement()) return build_sweep_statement(value);
        if (auto* value = context->measurementStatement()) return build_measurement_statement(value);
        if (auto* value = context->deriveStatement()) return build_derive_statement(value);
        if (auto* value = context->assertionStatement()) return build_assertion_statement(value);
        return build_calibration_statement(context->calibrationStatement());
    }

    ast::ReferenceStatement build_reference_statement(HorusRfParser::ReferenceStatementContext* context) {
        return {build_signed_quantity(context->signedQuantity()), source_map_.span(context)};
    }

    ast::SweepStatement build_sweep_statement(HorusRfParser::SweepStatementContext* context) {
        const auto quantities = context->signedQuantity();
        return {context->sweepDimension()->getText(), build_signed_quantity(quantities[0]),
                build_signed_quantity(quantities[1]), build_signed_quantity(quantities[2]),
                source_map_.span(context)};
    }

    ast::MeasurementStatement build_measurement_statement(
        HorusRfParser::MeasurementStatementContext* context) {
        if (context->POWER() != nullptr) {
            return {"power", "power", true, source_map_.span(context)};
        }
        const auto names = context->expressionName();
        return {names[0]->getText(), names[1]->getText(), false, source_map_.span(context)};
    }

    ast::DeriveStatement build_derive_statement(HorusRfParser::DeriveStatementContext* context) {
        return {context->IDENTIFIER()->getText(), build_expression(context->expression()),
                source_map_.span(context)};
    }

    ast::AssertionStatement build_assertion_statement(
        HorusRfParser::AssertionStatementContext* context) {
        return {build_expression(context->expression()), source_map_.span(context)};
    }

    ast::CalibrationStatement build_calibration_statement(
        HorusRfParser::CalibrationStatementContext* context) {
        std::vector<std::string> dimensions;
        dimensions.reserve(context->expressionName().size());
        for (auto* name : context->expressionName()) dimensions.push_back(name->getText());
        return {context->IDENTIFIER()->getText(), build_expression(context->expression()),
                std::move(dimensions), source_map_.span(context)};
    }

    ast::QuantityLiteral build_signed_quantity(HorusRfParser::SignedQuantityContext* context) {
        auto quantity = build_quantity(context->quantity());
        if (context->MINUS() != nullptr) {
            quantity.number.insert(quantity.number.begin(), '-');
            quantity.span = source_map_.span(context);
        }
        return quantity;
    }

    ast::QuantityLiteral build_quantity(HorusRfParser::QuantityContext* context) {
        return {context->NUMBER()->getText(), context->unit()->getText(), source_map_.span(context)};
    }

    ast::Expression build_expression(HorusRfParser::ExpressionContext* context) {
        return build_comparison(context->comparisonExpression());
    }

    ast::Expression build_comparison(HorusRfParser::ComparisonExpressionContext* context) {
        const auto operands = context->additiveExpression();
        auto left = build_additive(operands[0]);
        if (operands.size() == 1) return left;
        auto right = build_additive(operands[1]);
        ast::ComparisonOperator op = ast::ComparisonOperator::Equal;
        switch (context->comparisonOperator()->getStart()->getType()) {
        case HorusRfParser::LESS: op = ast::ComparisonOperator::Less; break;
        case HorusRfParser::LESS_EQUAL: op = ast::ComparisonOperator::LessEqual; break;
        case HorusRfParser::GREATER: op = ast::ComparisonOperator::Greater; break;
        case HorusRfParser::GREATER_EQUAL: op = ast::ComparisonOperator::GreaterEqual; break;
        case HorusRfParser::NOT_EQUAL: op = ast::ComparisonOperator::NotEqual; break;
        default: break;
        }
        const SourceSpan span{left.span.begin, right.span.end};
        return {ast::ComparisonExpression{op,
                    std::make_shared<ast::Expression>(std::move(left)),
                    std::make_shared<ast::Expression>(std::move(right)), span}, span};
    }

    ast::Expression build_additive(HorusRfParser::AdditiveExpressionContext* context) {
        const auto operands = context->unaryExpression();
        auto expression = build_unary(operands[0]);
        for (std::size_t index = 1; index < operands.size(); ++index) {
            auto right = build_unary(operands[index]);
            const auto operator_index = operands[index]->getStart()->getTokenIndex() - 1;
            const auto type = tokens_.get(operator_index)->getType();
            const SourceSpan span{expression.span.begin, right.span.end};
            ast::BinaryExpression binary{
                type == HorusRfParser::PLUS ? ast::BinaryOperator::Add
                                            : ast::BinaryOperator::Subtract,
                std::make_shared<ast::Expression>(std::move(expression)),
                std::make_shared<ast::Expression>(std::move(right)), span};
            expression = {std::move(binary), span};
        }
        return expression;
    }

    ast::Expression build_unary(HorusRfParser::UnaryExpressionContext* context) {
        auto operand = build_primary(context->primaryExpression());
        if (context->MINUS() == nullptr) return operand;
        const SourceSpan span{source_map_.span(context->MINUS()->getSymbol()).begin,
                              operand.span.end};
        return {ast::UnaryExpression{ast::UnaryOperator::Negate,
                    std::make_shared<ast::Expression>(std::move(operand)), span}, span};
    }

    ast::Expression build_primary(HorusRfParser::PrimaryExpressionContext* context) {
        if (auto* quantity = context->quantity()) {
            auto result = build_quantity(quantity);
            return {result, result.span};
        }
        if (auto* reference = context->reference()) {
            const auto span = source_map_.span(reference);
            return {ast::ReferenceExpression{"reference", reference->expressionName()->getText(), span}, span};
        }
        if (auto* call = context->call()) {
            auto argument = build_expression(call->expression());
            const auto span = source_map_.span(call);
            return {ast::CallExpression{call->expressionName()->getText(),
                        std::make_shared<ast::Expression>(std::move(argument)), span}, span};
        }
        if (auto* name = context->expressionName()) {
            const auto span = source_map_.span(name);
            return {ast::IdentifierExpression{name->getText(), span}, span};
        }
        auto expression = build_expression(context->expression());
        expression.span = source_map_.span(context);
        return expression;
    }

    const SourceMap& source_map_;
    antlr4::CommonTokenStream& tokens_;
};

} // namespace

std::vector<Token> lex(std::string_view source) {
    const std::string owned_source{source};
    SourceMap source_map{owned_source};
    antlr4::ANTLRInputStream input{owned_source};
    HorusRfLexer lexer{&input};
    lexer.removeErrorListeners();
    antlr4::CommonTokenStream tokens{&lexer};
    tokens.fill();

    std::vector<Token> result;
    result.reserve(tokens.getTokens().size());
    for (const auto* token : tokens.getTokens()) {
        const auto type = token->getType();
        result.push_back(Token{token_kind(type),
                               type == antlr4::Token::EOF ? std::string{} : token->getText(),
                               source_map.span(token)});
    }
    return result;
}

const std::vector<FixedTokenDefinition>& fixed_token_definitions() {
    static const auto definitions = [] {
        antlr4::ANTLRInputStream input;
        HorusRfLexer lexer{&input};
        const auto& vocabulary = lexer.getVocabulary();

        std::vector<FixedTokenDefinition> result;
        result.reserve(HorusRfLexer::COMMA - HorusRfLexer::CHARACTERIZE + 1);
        for (std::size_t type = HorusRfLexer::CHARACTERIZE;
             type <= HorusRfLexer::COMMA; ++type) {
            const auto kind = token_kind(type);
            result.push_back(
                {kind, fixed_token_category(kind), literal_spelling(vocabulary, type)});
        }
        return result;
    }();
    return definitions;
}

std::string_view token_kind_name(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::EndOfFile: return "end of file";
    case TokenKind::Identifier: return "identifier";
    case TokenKind::Number: return "number";
    case TokenKind::Characterize: return "characterize";
    case TokenKind::Reference: return "reference";
    case TokenKind::Power: return "power";
    case TokenKind::Sweep: return "sweep";
    case TokenKind::Frequency: return "frequency";
    case TokenKind::Temperature: return "temperature";
    case TokenKind::Step: return "step";
    case TokenKind::Measure: return "measure";
    case TokenKind::Derive: return "derive";
    case TokenKind::Calibration: return "calibration";
    case TokenKind::Correction: return "correction";
    case TokenKind::Over: return "over";
    case TokenKind::Assert: return "assert";
    case TokenKind::LeftBrace: return "{";
    case TokenKind::RightBrace: return "}";
    case TokenKind::Equal: return "=";
    case TokenKind::EqualEqual: return "==";
    case TokenKind::NotEqual: return "!=";
    case TokenKind::Less: return "<";
    case TokenKind::LessEqual: return "<=";
    case TokenKind::Greater: return ">";
    case TokenKind::GreaterEqual: return ">=";
    case TokenKind::Range: return "..";
    case TokenKind::Dot: return ".";
    case TokenKind::Plus: return "+";
    case TokenKind::Minus: return "-";
    case TokenKind::LeftParen: return "(";
    case TokenKind::RightParen: return ")";
    case TokenKind::Comma: return ",";
    case TokenKind::UnitHz: return "Hz";
    case TokenKind::UnitKHz: return "kHz";
    case TokenKind::UnitMHz: return "MHz";
    case TokenKind::UnitGHz: return "GHz";
    case TokenKind::UnitDBm: return "dBm";
    case TokenKind::UnitDB: return "dB";
    case TokenKind::UnitK: return "K";
    case TokenKind::UnitDegC: return "degC";
    case TokenKind::UnitDeltaK: return "deltaK";
    case TokenKind::UnitDeltaC: return "deltaC";
    case TokenKind::UnitPercent: return "%";
    case TokenKind::Invalid: return "invalid token";
    }
    return "unknown token";
}

bool is_unit(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::UnitHz:
    case TokenKind::UnitKHz:
    case TokenKind::UnitMHz:
    case TokenKind::UnitGHz:
    case TokenKind::UnitDBm:
    case TokenKind::UnitDB:
    case TokenKind::UnitK:
    case TokenKind::UnitDegC:
    case TokenKind::UnitDeltaK:
    case TokenKind::UnitDeltaC:
    case TokenKind::UnitPercent: return true;
    default: return false;
    }
}

std::string_view diagnostic_code_name(DiagnosticCode code) noexcept {
    switch (code) {
    case DiagnosticCode::UnexpectedToken: return "parse.unexpected_token";
    case DiagnosticCode::UnexpectedEndOfFile: return "parse.unexpected_end_of_file";
    case DiagnosticCode::InvalidToken: return "parse.invalid_token";
    case DiagnosticCode::ExpectedIdentifier: return "parse.expected_identifier";
    case DiagnosticCode::ExpectedQuantity: return "parse.expected_quantity";
    case DiagnosticCode::ExpectedUnit: return "parse.expected_unit";
    case DiagnosticCode::ExpectedExpression: return "parse.expected_expression";
    }
    return "parse.unknown";
}

ParseError::ParseError(Diagnostic diagnostic)
    : std::runtime_error(diagnostic.message), diagnostic_(std::move(diagnostic)) {}

ast::Program parse(std::string_view source) {
    const std::string owned_source{source};
    SourceMap source_map{owned_source};
    antlr4::ANTLRInputStream input{owned_source};
    HorusRfLexer lexer{&input};
    lexer.removeErrorListeners();
    antlr4::CommonTokenStream tokens{&lexer};
    HorusRfParser parser{&tokens};
    parser.removeErrorListeners();
    ErrorListener listener{source_map, parser};
    parser.addErrorListener(&listener);
    parser.setErrorHandler(std::make_shared<FailFastErrorStrategy>());
    auto* tree = parser.program();
    return AstBuilder{source_map, tokens}.build(tree);
}

} // namespace horusrf::parser
