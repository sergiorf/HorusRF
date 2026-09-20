#pragma once

#include <string>

#include "horusrf/parser/token.hpp"

namespace horusrf::runtime {

enum class DiagnosticCode {
    InvalidValueId,
    InvalidDefinitionOrder,
    TypeMismatch,
    InvalidProgramBinding,
    InvalidOperation,
    InvalidSweep,
    SweepTooLarge,
    InvalidCalibrationBinding,
};

struct Diagnostic {
    DiagnosticCode code{DiagnosticCode::InvalidOperation};
    std::string message;
    parser::SourceSpan span;
};

} // namespace horusrf::runtime
