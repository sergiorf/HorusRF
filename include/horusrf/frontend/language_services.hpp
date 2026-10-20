#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "horusrf/ast/ast.hpp"
#include "horusrf/parser/lexer.hpp"
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

struct LanguageSpec {
    std::string name;
    std::vector<std::string> grammar_rules;
    std::vector<parser::FixedTokenDefinition> fixed_tokens;
};

struct StatementCapability {
    std::string name;
};

struct MeasurementCapability {
    std::string operation;
    std::string result_type;
};

struct SweepCapability {
    std::string dimension;
    std::string endpoint_type;
    std::string step_type;
    bool required{};
};

struct UnitCapability {
    std::string spelling;
    std::string quantity_type;
};

struct FunctionCapability {
    std::string name;
    std::vector<std::string> argument_types;
    std::string result_type;
};

struct ConstraintCapability {
    std::string code;
    std::string description;
};

struct LanguageCapabilities {
    std::vector<StatementCapability> statements;
    std::vector<MeasurementCapability> measurements;
    std::vector<SweepCapability> sweep_dimensions;
    std::vector<UnitCapability> units;
    std::vector<FunctionCapability> functions;
    std::vector<ConstraintCapability> constraints;
};

[[nodiscard]] ParseResult parse(std::string_view source);
[[nodiscard]] ValidationResult validate(std::string_view source);
[[nodiscard]] std::vector<Diagnostic> get_diagnostics(std::string_view source);
[[nodiscard]] const LanguageSpec& get_language_spec();
[[nodiscard]] const LanguageCapabilities& get_language_capabilities();

} // namespace horusrf::frontend
