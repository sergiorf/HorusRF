#pragma once

#include <string>

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

struct Diagnostic {
    DiagnosticCode code{DiagnosticCode::InvalidNumericLiteral};
    std::string message;
    parser::SourceSpan span;
};

} // namespace horusrf::semantic
