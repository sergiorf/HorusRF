#include "horusrf/frontend/language_services.hpp"

#include <utility>

#include "horusrf/domain/units.hpp"
#include "horusrf/parser/parser.hpp"
#include "horusrf/semantic/analyzer.hpp"
#include "horusrf/semantic/language.hpp"

namespace horusrf::frontend {
namespace {

Diagnostic adapt(const parser::Diagnostic& diagnostic) {
    return {DiagnosticCategory::Syntax,
            std::string{parser::diagnostic_code_name(diagnostic.code)},
            diagnostic.message, diagnostic.span};
}

Diagnostic adapt(const semantic::Diagnostic& diagnostic) {
    return {DiagnosticCategory::Semantic,
            std::string{semantic::diagnostic_code_name(diagnostic.code)},
            diagnostic.message, diagnostic.span};
}

std::string quantity_type(domain::Dimension dimension) {
    switch (dimension) {
    case domain::Dimension::Frequency: return "Frequency";
    case domain::Dimension::AbsolutePower: return "Power";
    case domain::Dimension::RelativePower: return "PowerDelta";
    case domain::Dimension::Ratio: return "Ratio";
    case domain::Dimension::Temperature: return "Temperature";
    case domain::Dimension::TemperatureDelta: return "TemperatureDelta";
    }
    return "unknown";
}

} // namespace

ParseResult parse(std::string_view source) {
    try {
        return {parser::parse(source), {}};
    } catch (const parser::ParseError& error) {
        return {std::nullopt, {adapt(error.diagnostic())}};
    }
}

ValidationResult validate(std::string_view source) {
    auto parsed = parse(source);
    if (!parsed.ok()) return {std::nullopt, std::move(parsed.diagnostics)};

    auto analyzed = semantic::analyze(*parsed.program);
    std::vector<Diagnostic> diagnostics;
    diagnostics.reserve(analyzed.diagnostics.size());
    for (const auto& diagnostic : analyzed.diagnostics) {
        diagnostics.push_back(adapt(diagnostic));
    }
    return {std::move(analyzed.program), std::move(diagnostics)};
}

std::vector<Diagnostic> get_diagnostics(std::string_view source) {
    return validate(source).diagnostics;
}

const LanguageSpec& get_language_spec() {
    static const LanguageSpec spec{
        "HorusRF",
        parser::grammar_rule_names(),
        parser::fixed_token_definitions(),
    };
    return spec;
}

const LanguageCapabilities& get_language_capabilities() {
    static const auto capabilities = [] {
        LanguageCapabilities result;
        constexpr std::string_view statement_suffix = "Statement";
        for (const auto& rule : parser::grammar_rule_names()) {
            if (rule.ends_with(statement_suffix)) {
                result.statements.push_back(
                    {rule.substr(0, rule.size() - statement_suffix.size())});
            }
        }

        for (const auto& definition : semantic::measurement_definitions()) {
            result.measurements.push_back(
                {std::string{definition.name}, semantic::type_name(definition.result_type)});
        }
        for (const auto& definition : semantic::sweep_definitions()) {
            result.sweep_dimensions.push_back(
                {std::string{definition.name}, semantic::type_name(definition.endpoint_type),
                 semantic::type_name(definition.step_type), definition.required});
        }
        for (const auto& token : parser::fixed_token_definitions()) {
            if (token.category != parser::FixedTokenCategory::Unit) continue;
            const auto unit = domain::parse_unit(token.spelling);
            if (unit) {
                result.units.push_back(
                    {token.spelling, quantity_type(domain::dimension(*unit))});
            }
        }
        for (const auto& definition : semantic::function_definitions()) {
            FunctionCapability function{std::string{definition.name}, {}, "same as argument"};
            for (const auto type : definition.argument_types) {
                function.argument_types.emplace_back(semantic::type_name(type));
            }
            result.functions.push_back(std::move(function));
        }

        result.constraints = {
            {"required.reference_power", "Exactly one reference power is required."},
            {"required.frequency_sweep", "A frequency sweep is required."},
            {"required.power_measurement", "At least one power measurement is required."},
            {"required.calibration", "Exactly one calibration is required."},
            {"ordering.sweeps_before_measurements", "Sweeps must precede measurements."},
            {"sweep.positive_step", "Sweep steps must be strictly positive."},
            {"sweep.ascending_range", "Sweep ends must not precede their starts."},
            {"sweep.maximum_points",
             "Each axis and the Cartesian product are limited to " +
                 std::to_string(semantic::maximum_sweep_point_count) + " points."},
            {"calibration.all_sweeps_in_order",
             "Calibration dimensions must list every sweep in declaration order."},
            {"calibration.power_delta",
             "A calibration correction must have type PowerDelta."},
        };
        return result;
    }();
    return capabilities;
}

} // namespace horusrf::frontend
