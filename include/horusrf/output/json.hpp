#pragma once

#include <iosfwd>

#include "horusrf/domain/results.hpp"

namespace horusrf::output {

// Writes the deterministic horusrf.characterization-result schema version 1.
void write_json(std::ostream& output,
                const domain::CharacterizationResult& result);

} // namespace horusrf::output
