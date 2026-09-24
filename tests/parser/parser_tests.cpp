#include "test_support.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <variant>

#include "horusrf/parser/parser.hpp"

void run_lexer_tests();

namespace {

using namespace horusrf;

std::string read_fixture() {
    const std::string path = std::string{HORUSRF_SOURCE_DIR} +
                             "/examples/tx_path_characterization.hrf";
    std::ifstream input(path, std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

template <typename T>
const T& alternative(const ast::Statement& statement) {
    const auto* value = std::get_if<T>(&statement);
    CHECK(value != nullptr);
    return *value;
}

template <typename T>
const T& expression_as(const ast::Expression& expression) {
    const auto* value = std::get_if<T>(&expression.value);
    CHECK(value != nullptr);
    return *value;
}

parser::Diagnostic parse_failure(std::string_view source) {
    try {
        (void)parser::parse(source);
    } catch (const parser::ParseError& error) {
        return error.diagnostic();
    }
    throw std::runtime_error("expected parsing to fail");
}

void canonical_program_test() {
    const auto program = parser::parse(read_fixture());
    const auto& characterization = program.characterization;
    CHECK_EQ(characterization.name, "tx_path");
    CHECK_EQ(characterization.statements.size(), std::size_t{4});
    CHECK_EQ(program.span.begin.line, std::size_t{1});
    CHECK_EQ(program.span.begin.column, std::size_t{1});

    const auto& reference = alternative<ast::ReferenceStatement>(characterization.statements[0]);
    CHECK_EQ(reference.power.number, "-10");
    CHECK_EQ(reference.power.unit, "dBm");
    CHECK_EQ(reference.span.begin.line, std::size_t{3});

    const auto& sweep = alternative<ast::SweepStatement>(characterization.statements[1]);
    CHECK_EQ(sweep.start.number, "2.40");
    CHECK_EQ(sweep.start.unit, "GHz");
    CHECK_EQ(sweep.end.number, "2.50");
    CHECK_EQ(sweep.end.unit, "GHz");
    CHECK_EQ(sweep.step.number, "1");
    CHECK_EQ(sweep.step.unit, "MHz");

    (void)alternative<ast::MeasurementStatement>(characterization.statements[2]);

    const auto& calibration =
        alternative<ast::CalibrationStatement>(characterization.statements[3]);
    CHECK_EQ(calibration.name, "tx_power");
    const auto& correction = expression_as<ast::BinaryExpression>(calibration.correction);
    CHECK_EQ(correction.op, ast::BinaryOperator::Subtract);
    CHECK_EQ(expression_as<ast::ReferenceExpression>(*correction.left).member, "power");
    CHECK_EQ(expression_as<ast::IdentifierExpression>(*correction.right).name, "power");
    CHECK_EQ(calibration.dimensions.size(), std::size_t{1});
    CHECK_EQ(calibration.dimensions[0], "frequency");
    CHECK(calibration.correction.span.end.offset < calibration.span.end.offset);
}

void expression_tests() {
    const auto program = parser::parse(
        "characterize c { derive x = -a + b - (c + 1 dB) }");
    const auto& derive = alternative<ast::DeriveStatement>(
        program.characterization.statements.front());
    const auto& outer = expression_as<ast::BinaryExpression>(derive.expression);
    CHECK_EQ(outer.op, ast::BinaryOperator::Subtract);
    const auto& left = expression_as<ast::BinaryExpression>(*outer.left);
    CHECK_EQ(left.op, ast::BinaryOperator::Add);
    CHECK_EQ(expression_as<ast::UnaryExpression>(*left.left).op, ast::UnaryOperator::Negate);
    const auto& grouped = expression_as<ast::BinaryExpression>(*outer.right);
    CHECK_EQ(grouped.op, ast::BinaryOperator::Add);
    CHECK_EQ(expression_as<ast::QuantityLiteral>(*grouped.right).unit, "dB");
    CHECK_EQ(outer.right->span.begin.column, std::size_t{38});
}

void multiple_dimensions_test() {
    const auto program = parser::parse(
        "characterize c { derive calibration cal { correction = a over frequency, power } }");
    const auto& calibration = alternative<ast::CalibrationStatement>(
        program.characterization.statements.front());
    CHECK_EQ(expression_as<ast::IdentifierExpression>(calibration.correction).name, "a");
    CHECK_EQ(calibration.dimensions.size(), std::size_t{2});
    CHECK_EQ(calibration.dimensions[0], "frequency");
    CHECK_EQ(calibration.dimensions[1], "power");
}

void diagnostic_tests() {
    constexpr std::array mappings{
        std::pair{parser::DiagnosticCode::UnexpectedToken, "parse.unexpected_token"},
        std::pair{parser::DiagnosticCode::UnexpectedEndOfFile, "parse.unexpected_end_of_file"},
        std::pair{parser::DiagnosticCode::InvalidToken, "parse.invalid_token"},
        std::pair{parser::DiagnosticCode::ExpectedIdentifier, "parse.expected_identifier"},
        std::pair{parser::DiagnosticCode::ExpectedQuantity, "parse.expected_quantity"},
        std::pair{parser::DiagnosticCode::ExpectedUnit, "parse.expected_unit"},
        std::pair{parser::DiagnosticCode::ExpectedExpression, "parse.expected_expression"},
    };
    for (const auto& [code, name] : mappings) {
        CHECK_EQ(parser::diagnostic_code_name(code), name);
    }
    CHECK_EQ(parser::diagnostic_code_name(static_cast<parser::DiagnosticCode>(999)),
             "parse.unknown");

    {
        const auto diagnostic = parse_failure("characterize c {");
        CHECK_EQ(diagnostic.code, parser::DiagnosticCode::UnexpectedEndOfFile);
        CHECK_EQ(diagnostic.span.begin, (parser::SourcePosition{1, 17, 16}));
    }
    {
        const auto diagnostic = parse_failure("characterize c { reference power = 10 }");
        CHECK_EQ(diagnostic.code, parser::DiagnosticCode::ExpectedUnit);
        CHECK_EQ(diagnostic.span.begin.column, std::size_t{39});
    }
    {
        const auto diagnostic = parse_failure("characterize c { derive x = }");
        CHECK_EQ(diagnostic.code, parser::DiagnosticCode::ExpectedExpression);
        CHECK_EQ(diagnostic.span.begin.column, std::size_t{29});
    }
    {
        const auto diagnostic = parse_failure(
            "characterize c { derive calibration x { correction = a frequency } }");
        CHECK_EQ(diagnostic.code, parser::DiagnosticCode::UnexpectedToken);
        CHECK_EQ(diagnostic.span.begin.column, std::size_t{56});
    }
    {
        const auto diagnostic = parse_failure("characterize c { measure @ }");
        CHECK_EQ(diagnostic.code, parser::DiagnosticCode::InvalidToken);
        CHECK_EQ(diagnostic.span.begin.column, std::size_t{26});
    }
    {
        const auto diagnostic = parse_failure("characterize 7bad {}");
        CHECK_EQ(diagnostic.code, parser::DiagnosticCode::InvalidToken);
        CHECK_EQ(diagnostic.span.begin.column, std::size_t{14});
    }
}

} // namespace

int main() {
    try {
        run_lexer_tests();
        canonical_program_test();
        expression_tests();
        multiple_dimensions_test();
        diagnostic_tests();
        std::cout << "All HorusRF parser tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
