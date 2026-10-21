#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace horusrf::ai {

enum class SemanticComparisonStatus {
    Match,
    Mismatch,
    NotEvaluated,
};

struct SemanticDifference final {
    std::string path;
    std::string expected;
    std::string actual;
};

struct SemanticComparisonResult final {
    SemanticComparisonStatus status{SemanticComparisonStatus::NotEvaluated};
    std::vector<SemanticDifference> differences;

    [[nodiscard]] bool matches() const noexcept {
        return status == SemanticComparisonStatus::Match;
    }
};

// Compiles both scripts through parsing, semantic analysis, and IR lowering,
// then compares their observable characterization structure. Formatting,
// source locations, characterization names, and harmless identifier names are
// ignored. Invalid input is reported as NotEvaluated rather than as a match.
[[nodiscard]] SemanticComparisonResult compare_script_semantics(
    std::string_view expected_source, std::string_view actual_source);

} // namespace horusrf::ai
