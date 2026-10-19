#include "HorusRfLexer.h"
#include "HorusRfParser.h"

#include <antlr4-runtime.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "parser/regression_corpus.hpp"
#include "test_support.hpp"

namespace {

using namespace horusrf::test::parser_regression;

std::string read_source(std::string_view relative_path) {
    const auto path = std::filesystem::path{HORUSRF_SOURCE_DIR} / relative_path;
    std::ifstream input(path, std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

bool recognizes(std::string_view source) {
    antlr4::ANTLRInputStream input{std::string{source}};
    HorusRfLexer lexer{&input};
    lexer.removeErrorListeners();
    antlr4::CommonTokenStream tokens{&lexer};
    HorusRfParser parser{&tokens};
    parser.removeErrorListeners();
    (void)parser.program();
    return lexer.getNumberOfSyntaxErrors() == 0 && parser.getNumberOfSyntaxErrors() == 0;
}

std::vector<std::size_t> token_types(std::string_view source) {
    antlr4::ANTLRInputStream input{std::string{source}};
    HorusRfLexer lexer{&input};
    lexer.removeErrorListeners();
    antlr4::CommonTokenStream tokens{&lexer};
    tokens.fill();

    std::vector<std::size_t> result;
    for (const auto* token : tokens.getTokens()) {
        result.push_back(token->getType());
    }
    return result;
}

void shared_corpus_recognition_test() {
    for (const auto& example : example_cases) {
        CHECK(recognizes(read_source(example.path)));
    }
    for (const auto source : valid_sources) {
        CHECK(recognizes(source));
    }
    for (const auto source : invalid_sources) {
        CHECK(!recognizes(source));
    }
}

void lexical_contract_test() {
    const auto malformed = token_types("2. .5 1.2.3 1.abc 12abc 1Hz");
    const std::vector<std::size_t> expected_malformed{
        HorusRfLexer::MALFORMED_NUMBER,
        HorusRfLexer::MALFORMED_NUMBER,
        HorusRfLexer::MALFORMED_NUMBER,
        HorusRfLexer::MALFORMED_NUMBER,
        HorusRfLexer::MALFORMED_NUMBER,
        HorusRfLexer::MALFORMED_NUMBER,
        antlr4::Token::EOF,
    };
    CHECK_EQ(malformed, expected_malformed);

    const auto boundary = token_types("1 Hz 1..2 Hz 1.2..3 @");
    const std::vector<std::size_t> expected_boundary{
        HorusRfLexer::NUMBER,
        HorusRfLexer::UNIT_HZ,
        HorusRfLexer::NUMBER,
        HorusRfLexer::RANGE,
        HorusRfLexer::NUMBER,
        HorusRfLexer::UNIT_HZ,
        HorusRfLexer::NUMBER,
        HorusRfLexer::RANGE,
        HorusRfLexer::NUMBER,
        HorusRfLexer::INVALID,
        antlr4::Token::EOF,
    };
    CHECK_EQ(boundary, expected_boundary);

    CHECK(recognizes("// comment\r\n\tcharacterize c {\v\f measure power }"));
    CHECK(recognizes(
        "characterize c { derive x = power(frequency) + temperature "
        "derive calibration cal { correction = reference.power over frequency, power } }"));
}

} // namespace

int main() {
    try {
        shared_corpus_recognition_test();
        lexical_contract_test();
        std::cout << "All ANTLR grammar recognition tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
