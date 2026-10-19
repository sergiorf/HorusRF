#include "horusrf/frontend/language_services.hpp"

#include <utility>

#include "horusrf/parser/parser.hpp"
#include "horusrf/semantic/analyzer.hpp"

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

} // namespace horusrf::frontend
