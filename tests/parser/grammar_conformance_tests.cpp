#include "test_support.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>

#include "horusrf/parser/parser.hpp"
#include "regression_corpus.hpp"

namespace {

using namespace horusrf::test::parser_regression;

std::string read_source(std::string_view relative_path) {
    const auto path = std::string{HORUSRF_SOURCE_DIR} + "/" + std::string{relative_path};
    std::ifstream input(path, std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

bool parses(std::string_view source) {
    try {
        (void)horusrf::parser::parse(source);
        return true;
    } catch (const horusrf::parser::ParseError&) {
        return false;
    }
}

void all_examples_are_classified() {
    std::set<std::string> classified;
    for (const auto& example : example_cases) {
        CHECK(classified.insert(std::string{example.path}).second);
    }

    std::set<std::string> discovered;
    const auto source_root = std::filesystem::path{HORUSRF_SOURCE_DIR};
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(source_root / "examples")) {
        if (entry.is_regular_file() && entry.path().extension() == ".hrf") {
            discovered.insert(entry.path().lexically_relative(source_root).generic_string());
        }
    }
    CHECK_EQ(classified, discovered);
}

} // namespace

void run_grammar_conformance_tests() {
    all_examples_are_classified();

    // Semantic-error examples must still cross the syntax boundary successfully.
    // Their classification is consumed by semantic/CLI suites independently.
    for (const auto& example : example_cases) {
        CHECK(parses(read_source(example.path)));
    }
    for (const auto source : valid_sources) {
        CHECK(parses(source));
    }
    for (const auto source : invalid_sources) {
        CHECK(!parses(source));
    }
}
