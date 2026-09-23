#pragma once

#include <string>
#include <string_view>

#include "horusrf/parser/token.hpp"

namespace horusrf::ir {

enum class DiagnosticCode {
    DuplicateSymbol,
    UnknownSymbol,
    TypeMismatch,
    InvalidDefinitionOrder,
    InvalidCalibrationIndex,
    UnsupportedExpression,
};

[[nodiscard]] std::string_view diagnostic_code_name(DiagnosticCode code) noexcept;

struct Diagnostic {
    DiagnosticCode code{DiagnosticCode::UnsupportedExpression};
    std::string message;
    parser::SourceSpan span;
};

} // namespace horusrf::ir
