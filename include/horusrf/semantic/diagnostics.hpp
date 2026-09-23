#pragma once

#include <string>
#include <string_view>

#include "horusrf/parser/token.hpp"

namespace horusrf::semantic {

enum class DiagnosticCode {
    InvalidNumericLiteral,
    UnexpectedQuantityType,
    DuplicateDeclaration,
    MissingReference,
    MissingSweep,
    MissingMeasurement,
    UnknownIdentifier,
    UnknownReferenceMember,
    ReferenceNotAvailable,
    InvalidUnaryOperand,
    InvalidBinaryOperands,
    InvalidSweepRange,
    InvalidSweepStep,
    UnknownCalibrationDimension,
    NonSweepCalibrationDimension,
    DuplicateCalibrationDimension,
    InvalidCalibrationCorrection,
};

[[nodiscard]] std::string_view diagnostic_code_name(DiagnosticCode code) noexcept;

struct Diagnostic {
    DiagnosticCode code{DiagnosticCode::InvalidNumericLiteral};
    std::string message;
    parser::SourceSpan span;
};

} // namespace horusrf::semantic
