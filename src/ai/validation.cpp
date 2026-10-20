#include "horusrf/ai/validation.hpp"

#include <ios>
#include <locale>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "horusrf/frontend/language_services.hpp"
#include "horusrf/ir/lowering.hpp"

namespace horusrf::ai {
namespace {

SourcePosition adapt(const parser::SourcePosition& position) {
    return {position.line, position.column, position.offset};
}

SourceSpan adapt(const parser::SourceSpan& span) {
    return {adapt(span.begin), adapt(span.end)};
}

ValidationDiagnostic adapt(const frontend::Diagnostic& diagnostic) {
    const auto category = diagnostic.category == frontend::DiagnosticCategory::Syntax
                              ? DiagnosticCategory::Syntax
                              : DiagnosticCategory::Semantic;
    return {category, diagnostic.code, diagnostic.message, adapt(diagnostic.span)};
}

ValidationDiagnostic adapt(const ir::Diagnostic& diagnostic) {
    return {DiagnosticCategory::Lowering,
            std::string{ir::diagnostic_code_name(diagnostic.code)},
            diagnostic.message,
            adapt(diagnostic.span)};
}

std::string_view category_name(DiagnosticCategory category) {
    switch (category) {
    case DiagnosticCategory::Syntax: return "syntax";
    case DiagnosticCategory::Semantic: return "semantic";
    case DiagnosticCategory::Lowering: return "lowering";
    }
    return "unknown";
}

void write_string(std::ostream& output, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    output.put('"');
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (ch < 0x20) {
                output << "\\u00" << hex[ch >> 4] << hex[ch & 0x0f];
            } else {
                output.put(static_cast<char>(ch));
            }
        }
    }
    output.put('"');
}

void write_position(std::ostream& output, const SourcePosition& position) {
    output << "{\"line\":" << position.line
           << ",\"column\":" << position.column
           << ",\"offset\":" << position.offset << '}';
}

} // namespace

ValidationResult validate_script(std::string_view source) {
    auto validated = frontend::validate(source);
    if (!validated.ok()) {
        std::vector<ValidationDiagnostic> diagnostics;
        diagnostics.reserve(validated.diagnostics.size());
        for (const auto& diagnostic : validated.diagnostics) {
            diagnostics.push_back(adapt(diagnostic));
        }
        return ValidationResult::invalid(std::move(diagnostics));
    }

    auto lowered = ir::lower(*validated.program);
    if (!lowered.ok()) {
        std::vector<ValidationDiagnostic> diagnostics;
        diagnostics.reserve(lowered.diagnostics.size());
        for (const auto& diagnostic : lowered.diagnostics) {
            diagnostics.push_back(adapt(diagnostic));
        }
        return ValidationResult::invalid(std::move(diagnostics));
    }
    return ValidationResult::valid();
}

void write_validation_json(std::ostream& output, const ValidationResult& result) {
    const auto previous_locale = output.getloc();
    output.imbue(std::locale::classic());

    output << "{\"schema\":\"horusrf.validation-result\",\"version\":1,\"valid\":"
           << (result.ok() ? "true" : "false") << ",\"diagnostics\":[";
    const auto& diagnostics = result.diagnostics();
    for (std::size_t index = 0; index < diagnostics.size(); ++index) {
        if (index != 0) output.put(',');
        const auto& diagnostic = diagnostics[index];
        output << "{\"category\":";
        write_string(output, category_name(diagnostic.category));
        output << ",\"code\":";
        write_string(output, diagnostic.code);
        output << ",\"message\":";
        write_string(output, diagnostic.message);
        output << ",\"range\":{\"start\":";
        write_position(output, diagnostic.span.begin);
        output << ",\"end\":";
        write_position(output, diagnostic.span.end);
        output << "}}";
    }
    output << "]}\n";
    output.imbue(previous_locale);
    if (!output) throw std::ios_base::failure("failed to write validation JSON");
}

} // namespace horusrf::ai
