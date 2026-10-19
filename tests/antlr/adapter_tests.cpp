#include "antlr_parser.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include "horusrf/parser/parser.hpp"
#include "parser/regression_support.hpp"
#include "parser/regression_corpus.hpp"
#include "test_support.hpp"

namespace {

using namespace horusrf;
using namespace horusrf::test::parser_regression;

constexpr std::string_view complete_ast_source =
    "characterize full {\r\n"
    "reference power = -10 dBm\n"
    "sweep frequency 1 Hz .. 2 GHz step 3 MHz\n"
    "measure power\n"
    "measure named = evm\n"
    "derive calc = -a + b - (reference.power + abs(1 dB))\n"
    "assert a < b\n"
    "assert a <= b\n"
    "assert a > b\n"
    "assert a >= b\n"
    "assert a == b\n"
    "assert a != b\n"
    "derive calibration cal { correction = calc over frequency, temperature }\n"
    "}";

parser::Diagnostic parse_failure(std::string_view source) {
    try {
        (void)parser::antlr_frontend::parse(source);
    } catch (const parser::ParseError& error) {
        return error.diagnostic();
    }
    throw std::runtime_error("expected ANTLR parsing to fail");
}

std::string read_source(std::string_view relative_path) {
    const auto path = std::filesystem::path{HORUSRF_SOURCE_DIR} / relative_path;
    std::ifstream input(path, std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void complete_ast_adapter_test() {
    const auto expected = ast_snapshot(parser::parse(complete_ast_source));
    const auto actual = ast_snapshot(parser::antlr_frontend::parse(complete_ast_source));
    if (actual != expected) {
        throw std::runtime_error("ANTLR AST adapter mismatch:\nexpected:\n" + expected +
                                 "\nactual:\n" + actual);
    }
}

void diagnostic_adapter_test() {
    struct Case {
        std::string_view source;
        parser::DiagnosticCode code;
        parser::SourceSpan span;
    };
    constexpr std::array cases{
        Case{"characterize c { bogus }", parser::DiagnosticCode::UnexpectedToken,
             {{1, 18, 17}, {1, 23, 22}}},
        Case{"characterize c {", parser::DiagnosticCode::UnexpectedEndOfFile,
             {{1, 17, 16}, {1, 17, 16}}},
        Case{"characterize c { measure @ }", parser::DiagnosticCode::InvalidToken,
             {{1, 26, 25}, {1, 27, 26}}},
        Case{"characterize power {}", parser::DiagnosticCode::ExpectedIdentifier,
             {{1, 14, 13}, {1, 19, 18}}},
        Case{"characterize c { reference power = dBm }", parser::DiagnosticCode::ExpectedQuantity,
             {{1, 36, 35}, {1, 39, 38}}},
        Case{"characterize c { reference power = 10 }", parser::DiagnosticCode::ExpectedUnit,
             {{1, 39, 38}, {1, 40, 39}}},
        Case{"characterize c { derive x = }", parser::DiagnosticCode::ExpectedExpression,
             {{1, 29, 28}, {1, 30, 29}}},
    };

    for (std::size_t index = 0; index < cases.size(); ++index) {
        const auto diagnostic = parse_failure(cases[index].source);
        if (diagnostic.code != cases[index].code) {
            throw std::runtime_error("ANTLR diagnostic code mismatch at case " +
                                     std::to_string(index) + ": expected " +
                                     std::string{parser::diagnostic_code_name(cases[index].code)} +
                                     ", got " +
                                     std::string{parser::diagnostic_code_name(diagnostic.code)});
        }
        if (diagnostic.span != cases[index].span) {
            throw std::runtime_error("ANTLR diagnostic span mismatch at case " +
                                     std::to_string(index) + ": " +
                                     diagnostic_snapshot(diagnostic));
        }
    }
}

void shared_corpus_construction_test() {
    for (const auto& example : example_cases) {
        (void)parser::antlr_frontend::parse(read_source(example.path));
    }
    for (const auto source : valid_sources) {
        (void)parser::antlr_frontend::parse(source);
    }
    for (const auto source : invalid_sources) {
        (void)parse_failure(source);
    }
}

void byte_offset_diagnostic_test() {
    const auto diagnostic = parse_failure("characterize c { measure \xC3\xA9 }");
    CHECK_EQ(diagnostic.code, parser::DiagnosticCode::InvalidToken);
    const parser::SourceSpan expected{{1, 26, 25}, {1, 28, 27}};
    CHECK_EQ(diagnostic.span, expected);
}

void public_parser_remains_handwritten_test() {
    const auto public_program = parser::parse("characterize still_handwritten {}");
    CHECK_EQ(public_program.characterization.name, "still_handwritten");
}

} // namespace

int main() {
    try {
        complete_ast_adapter_test();
        diagnostic_adapter_test();
        shared_corpus_construction_test();
        byte_offset_diagnostic_test();
        public_parser_remains_handwritten_test();
        std::cout << "All ANTLR adapter tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
