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
    UnknownMeasurementOperation,
    MissingCalibration,
    UnknownIdentifier,
    UnknownReferenceMember,
    ReferenceNotAvailable,
    InvalidUnaryOperand,
    InvalidBinaryOperands,
    InvalidSweepRange,
    InvalidSweepStep,
    SweepTooLarge,
    UnknownCalibrationDimension,
    NonSweepCalibrationDimension,
    DuplicateCalibrationDimension,
    InvalidCalibrationDimensions,
    InvalidCalibrationCorrection,
};

[[nodiscard]] std::string_view diagnostic_code_name(DiagnosticCode code) noexcept;

struct Diagnostic {
    DiagnosticCode code{DiagnosticCode::InvalidNumericLiteral};
    std::string message;
    parser::SourceSpan span;
};

} // namespace horusrf::semantic
