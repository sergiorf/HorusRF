#include "test_support.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "horusrf/ai/validation.hpp"

namespace {

using namespace horusrf;

constexpr std::string_view valid_source = R"(
characterize valid {
  reference power = -10 dBm
  sweep frequency 1 GHz .. 2 GHz step 1 GHz
  measure power
  derive calibration cal {
    correction = reference.power - power over frequency
  }
}
)";

bool has_code(const ai::ValidationResult& result, std::string_view code) {
    for (const auto& diagnostic : result.diagnostics()) {
        if (diagnostic.code == code) return true;
    }
    return false;
}

void valid_script_test() {
    const auto result = ai::validate_script(valid_source);
    CHECK(result.ok());
    CHECK(result.diagnostics().empty());

    std::ostringstream json;
    ai::write_validation_json(json, result);
    CHECK_EQ(json.str(),
             "{\"schema\":\"horusrf.validation-result\",\"version\":1,"
             "\"valid\":true,\"diagnostics\":[]}\n");
}

void syntax_error_and_source_range_test() {
    const auto result = ai::validate_script("characterize broken { measure @ }");
    CHECK(!result.ok());
    CHECK_EQ(result.diagnostics().size(), std::size_t{1});
    const auto& diagnostic = result.diagnostics().front();
    CHECK_EQ(diagnostic.category, ai::DiagnosticCategory::Syntax);
    CHECK_EQ(diagnostic.code, "parse.invalid_token");
    CHECK_EQ(diagnostic.span.begin, (ai::SourcePosition{1, 31, 30}));
    CHECK_EQ(diagnostic.span.end, (ai::SourcePosition{1, 32, 31}));
}

void representative_semantic_diagnostics_test() {
    auto result = ai::validate_script(R"(
characterize broken {
  reference power = 1 dB
  sweep frequency 2 GHz .. 1 GHz step 0 GHz
}
)");
    CHECK(!result.ok());
    CHECK_EQ(result.diagnostics().size(), std::size_t{5});
    CHECK(has_code(result, "semantic.unexpected_quantity_type"));
    CHECK(has_code(result, "semantic.invalid_sweep_step"));
    CHECK(has_code(result, "semantic.missing_measurement"));
    CHECK(has_code(result, "semantic.missing_calibration"));
    for (const auto& diagnostic : result.diagnostics()) {
        CHECK_EQ(diagnostic.category, ai::DiagnosticCategory::Semantic);
    }

    result = ai::validate_script(
        "characterize c { reference power = 0 dBm "
        "sweep frequency 1 Hz .. 2 Hz step 1 Hz measure power "
        "derive x = missing derive calibration cal { correction = power over frequency } }");
    CHECK(has_code(result, "semantic.unknown_identifier"));
    CHECK(has_code(result, "semantic.invalid_calibration_correction"));

    result = ai::validate_script(
        "characterize c { reference power = 0 dBm "
        "sweep frequency 1 Hz .. 2 Hz step 1 Hz measure power "
        "derive d = power - reference.power "
        "derive calibration cal { correction = d over power } }");
    CHECK(has_code(result, "semantic.non_sweep_calibration_dimension"));
}

void json_escaping_test() {
    const auto result = ai::ValidationResult::invalid({
        {ai::DiagnosticCategory::Lowering,
         "lowering.test",
         "quote \" slash \\ newline\n tab\t control \x01",
         {{2, 3, 4}, {5, 6, 7}}},
    });
    std::ostringstream json;
    ai::write_validation_json(json, result);
    CHECK_EQ(json.str(),
             "{\"schema\":\"horusrf.validation-result\",\"version\":1,"
             "\"valid\":false,\"diagnostics\":[{\"category\":\"lowering\","
             "\"code\":\"lowering.test\",\"message\":"
             "\"quote \\\" slash \\\\ newline\\n tab\\t control \\u0001\","
             "\"range\":{\"start\":{\"line\":2,\"column\":3,\"offset\":4},"
             "\"end\":{\"line\":5,\"column\":6,\"offset\":7}}}]}\n");
}

} // namespace

int main() {
    try {
        valid_script_test();
        syntax_error_and_source_range_test();
        representative_semantic_diagnostics_test();
        json_escaping_test();
        std::cout << "All HorusRF AI validation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
