#pragma once

#include <optional>
#include <variant>
#include <vector>

#include "horusrf/device/measurement_device.hpp"
#include "horusrf/device/rf_device.hpp"
#include "horusrf/domain/quantity.hpp"
#include "horusrf/ir/ir.hpp"
#include "horusrf/runtime/diagnostics.hpp"

namespace horusrf::runtime {

using RuntimeValue =
    std::variant<domain::Frequency, domain::Power, domain::PowerDelta>;

struct PointEvaluation {
    domain::Frequency frequency;
    std::vector<RuntimeValue> values;

    [[nodiscard]] const RuntimeValue& at(ir::ValueId id) const;
};

struct ExecutionResult {
    std::optional<PointEvaluation> evaluation;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept {
        return evaluation.has_value() && diagnostics.empty();
    }
};

[[nodiscard]] ExecutionResult execute_point(const ir::Program& program,
                                            domain::Frequency frequency,
                                            device::RfDevice& tester,
                                            device::MeasurementDevice& measurement);

} // namespace horusrf::runtime
