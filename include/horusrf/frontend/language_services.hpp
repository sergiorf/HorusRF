#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "horusrf/ast/ast.hpp"
#include "horusrf/parser/token.hpp"
#include "horusrf/semantic/model.hpp"

namespace horusrf::frontend {

enum class DiagnosticCategory { Syntax, Semantic };

struct Diagnostic {
    DiagnosticCategory category{DiagnosticCategory::Syntax};
    std::string code;
    std::string message;
    parser::SourceSpan span;
};

struct ParseResult {
    std::optional<ast::Program> program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept {
        return program.has_value() && diagnostics.empty();
    }
};

struct ValidationResult {
    std::optional<semantic::AnalyzedProgram> program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept {
        return program.has_value() && diagnostics.empty();
    }
};

[[nodiscard]] ParseResult parse(std::string_view source);
[[nodiscard]] ValidationResult validate(std::string_view source);

} // namespace horusrf::frontend
