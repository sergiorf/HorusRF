#pragma once

#include <optional>
#include <vector>

#include "horusrf/ir/diagnostics.hpp"
#include "horusrf/ir/ir.hpp"

namespace horusrf::semantic {
struct AnalyzedProgram;
}

namespace horusrf::ir {

struct LoweringResult {
    std::optional<Program> program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept {
        return program.has_value() && diagnostics.empty();
    }
};

[[nodiscard]] LoweringResult lower(const semantic::AnalyzedProgram& program);

} // namespace horusrf::ir
