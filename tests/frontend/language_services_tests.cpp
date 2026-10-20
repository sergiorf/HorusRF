#include "test_support.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

void diagnostics_convenience_test() {
    const std::string_view source = "characterize broken { measure @ }";
    const auto validation = frontend::validate(source);
    const auto diagnostics = frontend::get_diagnostics(source);
    CHECK_EQ(diagnostics.size(), validation.diagnostics.size());
    CHECK_EQ(diagnostics.front().category, validation.diagnostics.front().category);
    CHECK_EQ(diagnostics.front().code, validation.diagnostics.front().code);
    CHECK_EQ(diagnostics.front().message, validation.diagnostics.front().message);
    CHECK_EQ(diagnostics.front().span, validation.diagnostics.front().span);
    CHECK(frontend::get_diagnostics(valid_source).empty());
}

void language_spec_test() {
    const auto& spec = frontend::get_language_spec();
    CHECK_EQ(spec.name, "HorusRF");
    CHECK(!spec.grammar_rules.empty());
    CHECK_EQ(spec.grammar_rules.front(), "program");

    bool has_statement_rule = false;
    bool has_characterize = false;
    bool has_ghz = false;
    for (const auto& rule : spec.grammar_rules) {
        if (rule == "statement") has_statement_rule = true;
    }
    for (const auto& token : spec.fixed_tokens) {
        if (token.spelling == "characterize" &&
            token.category == parser::FixedTokenCategory::Keyword) {
            has_characterize = true;
        }
        if (token.spelling == "GHz" &&
            token.category == parser::FixedTokenCategory::Unit) {
            has_ghz = true;
        }
    }
    CHECK(has_statement_rule);
    CHECK(has_characterize);
    CHECK(has_ghz);
    CHECK(&spec == &frontend::get_language_spec());
}

void language_capabilities_test() {
    const auto& capabilities = frontend::get_language_capabilities();
    CHECK_EQ(capabilities.statements.size(), std::size_t{6});
    CHECK_EQ(capabilities.statements[0].name, "reference");
    CHECK_EQ(capabilities.statements[1].name, "sweep");
    CHECK_EQ(capabilities.statements[2].name, "measurement");
    CHECK_EQ(capabilities.statements[3].name, "derive");
    CHECK_EQ(capabilities.statements[4].name, "assertion");
    CHECK_EQ(capabilities.statements[5].name, "calibration");

    CHECK_EQ(capabilities.measurements.size(), std::size_t{3});
    CHECK_EQ(capabilities.measurements[0].operation, "power");
    CHECK_EQ(capabilities.measurements[0].result_type, "Power");
    CHECK_EQ(capabilities.measurements[1].operation, "evm");
    CHECK_EQ(capabilities.measurements[1].result_type, "Ratio");
    CHECK_EQ(capabilities.measurements[2].operation, "frequency_error");
    CHECK_EQ(capabilities.measurements[2].result_type, "FrequencyDelta");

    CHECK_EQ(capabilities.sweep_dimensions.size(), std::size_t{2});
    CHECK_EQ(capabilities.sweep_dimensions[0].dimension, "frequency");
    CHECK_EQ(capabilities.sweep_dimensions[0].endpoint_type, "Frequency");
    CHECK_EQ(capabilities.sweep_dimensions[0].step_type, "Frequency");
    CHECK(capabilities.sweep_dimensions[0].required);
    CHECK_EQ(capabilities.sweep_dimensions[1].dimension, "temperature");
    CHECK_EQ(capabilities.sweep_dimensions[1].endpoint_type, "Temperature");
    CHECK_EQ(capabilities.sweep_dimensions[1].step_type, "TemperatureDelta");
    CHECK(!capabilities.sweep_dimensions[1].required);

    const std::vector<std::pair<std::string, std::string>> expected_units{
        {"Hz", "Frequency"}, {"kHz", "Frequency"}, {"MHz", "Frequency"},
        {"GHz", "Frequency"}, {"dBm", "Power"}, {"dB", "PowerDelta"},
        {"K", "Temperature"}, {"degC", "Temperature"},
        {"deltaK", "TemperatureDelta"}, {"deltaC", "TemperatureDelta"},
        {"%", "Ratio"},
    };
    CHECK_EQ(capabilities.units.size(), expected_units.size());
    for (std::size_t index = 0; index < expected_units.size(); ++index) {
        CHECK_EQ(capabilities.units[index].spelling, expected_units[index].first);
        CHECK_EQ(capabilities.units[index].quantity_type, expected_units[index].second);
    }

    CHECK_EQ(capabilities.functions.size(), std::size_t{1});
    CHECK_EQ(capabilities.functions.front().name, "abs");
    CHECK_EQ(capabilities.functions.front().argument_types.size(), std::size_t{4});
    CHECK_EQ(capabilities.functions.front().result_type, "same as argument");

    CHECK_EQ(capabilities.constraints.size(), std::size_t{10});
    CHECK_EQ(capabilities.constraints[7].code, "sweep.maximum_points");
    CHECK(capabilities.constraints[7].description.find("1000000") != std::string::npos);
    CHECK(&capabilities == &frontend::get_language_capabilities());
}

} // namespace

int main() {
    try {
        successful_parse_and_validation_test();
        syntax_diagnostic_test();
        semantic_diagnostic_test();
        parse_does_not_run_semantics_test();
        diagnostics_convenience_test();
        language_spec_test();
        language_capabilities_test();
        std::cout << "All HorusRF language-service tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
