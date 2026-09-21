#pragma once

#include "horusrf/device/rf_device.hpp"
#include "horusrf/domain/results.hpp"

namespace horusrf::examples {

[[nodiscard]] domain::CharacterizationResult
run_tx_path_characterization(device::RfDevice& device);

} // namespace horusrf::examples
