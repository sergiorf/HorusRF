#pragma once

#include <string>

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

struct Diagnostic {
    DiagnosticCode code{DiagnosticCode::UnsupportedExpression};
    std::string message;
    parser::SourceSpan span;
};

} // namespace horusrf::ir
