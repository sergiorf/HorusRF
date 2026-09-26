#pragma once

#include <vector>

#include "horusrf/runtime/executor.hpp"

namespace horusrf::runtime::detail {

[[nodiscard]] std::vector<Diagnostic> validate_program(const ir::Program& program);

// The caller must first obtain an empty result from validate_program.
[[nodiscard]] PointEvaluation evaluate_validated_point(
    const ir::Program& program, domain::Frequency frequency, device::RfDevice& tester,
    device::MeasurementDevice& measurement);

} // namespace horusrf::runtime::detail
