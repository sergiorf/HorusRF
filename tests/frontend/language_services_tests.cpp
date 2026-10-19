#include "test_support.hpp"

#include <iostream>
#include <string_view>

#include "horusrf/frontend/language_services.hpp"

namespace {

using namespace horusrf;

constexpr std::string_view valid_source = R"(
characterize service_test {
  reference power = -10 dBm
  sweep frequency 1 GHz .. 2 GHz step 1 GHz
  measure power
  derive calibration cal {
    correction = reference.power - power over frequency
  }
}
)";

void successful_parse_and_validation_test() {
    const auto parsed = frontend::parse(valid_source);
    CHECK(parsed.ok());
    CHECK(parsed.diagnostics.empty());
    CHECK_EQ(parsed.program->characterization.name, "service_test");

    const auto validated = frontend::validate(valid_source);
    CHECK(validated.ok());
    CHECK(validated.diagnostics.empty());
    CHECK_EQ(validated.program->characterization_name, "service_test");
}

void syntax_diagnostic_test() {
    const auto result = frontend::validate("characterize broken { measure @ }");
    CHECK(!result.ok());
    CHECK(!result.program.has_value());
    CHECK_EQ(result.diagnostics.size(), std::size_t{1});
    const auto& diagnostic = result.diagnostics.front();
    CHECK_EQ(diagnostic.category, frontend::DiagnosticCategory::Syntax);
    CHECK_EQ(diagnostic.code, "parse.invalid_token");
    CHECK_EQ(diagnostic.message, "invalid token '@'");
    CHECK_EQ(diagnostic.span.begin, (parser::SourcePosition{1, 31, 30}));
    CHECK_EQ(diagnostic.span.end, (parser::SourcePosition{1, 32, 31}));
}

void semantic_diagnostic_test() {
    const auto result = frontend::validate(R"(
characterize broken {
  reference power = 1 dB
  sweep frequency 2 GHz .. 1 GHz step 0 GHz
}
)");
    CHECK(!result.ok());
    CHECK(!result.program.has_value());
    CHECK_EQ(result.diagnostics.size(), std::size_t{5});
    for (const auto& diagnostic : result.diagnostics) {
        CHECK_EQ(diagnostic.category, frontend::DiagnosticCategory::Semantic);
        CHECK(diagnostic.code.starts_with("semantic."));
    }
    CHECK_EQ(result.diagnostics[0].code, "semantic.unexpected_quantity_type");
    CHECK_EQ(result.diagnostics[0].span.begin,
             (parser::SourcePosition{3, 21, 43}));
    CHECK_EQ(result.diagnostics[1].code, "semantic.invalid_sweep_step");
    CHECK_EQ(result.diagnostics[2].code, "semantic.invalid_sweep_range");
    CHECK_EQ(result.diagnostics[3].code, "semantic.missing_measurement");
    CHECK_EQ(result.diagnostics[4].code, "semantic.missing_calibration");
}

void parse_does_not_run_semantics_test() {
    const auto result = frontend::parse(
        "characterize parsed { measure result = mystery }");
    CHECK(result.ok());
    CHECK(result.diagnostics.empty());
}

} // namespace

int main() {
    try {
        successful_parse_and_validation_test();
        syntax_diagnostic_test();
        semantic_diagnostic_test();
        parse_does_not_run_semantics_test();
        std::cout << "All HorusRF language-service tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
