#pragma once

#include <optional>
#include <vector>

#include "horusrf/ast/ast.hpp"
#include "horusrf/semantic/diagnostics.hpp"
#include "horusrf/semantic/model.hpp"

namespace horusrf::semantic {

struct AnalysisResult {
    std::optional<AnalyzedProgram> program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept {
        return program.has_value() && diagnostics.empty();
    }
};

[[nodiscard]] AnalysisResult analyze(const ast::Program& program);

} // namespace horusrf::semantic
