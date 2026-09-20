#pragma once

#include <optional>
#include <vector>

#include "horusrf/device/rf_device.hpp"
#include "horusrf/domain/results.hpp"
#include "horusrf/ir/ir.hpp"
#include "horusrf/runtime/diagnostics.hpp"

namespace horusrf::runtime {

struct CharacterizationExecutionResult {
    std::optional<domain::CharacterizationResult> result;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept {
        return result.has_value() && diagnostics.empty();
    }
};

[[nodiscard]] CharacterizationExecutionResult execute_characterization(
    const ir::Program& program, device::RfDevice& device);

} // namespace horusrf::runtime
