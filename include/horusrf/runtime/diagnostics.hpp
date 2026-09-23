#pragma once

#include <string>
#include <string_view>

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

[[nodiscard]] std::string_view diagnostic_code_name(DiagnosticCode code) noexcept;

struct Diagnostic {
    DiagnosticCode code{DiagnosticCode::InvalidOperation};
    std::string message;
    parser::SourceSpan span;
};

} // namespace horusrf::runtime
