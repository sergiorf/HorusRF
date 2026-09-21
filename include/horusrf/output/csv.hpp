#pragma once

#include <iosfwd>

#include "horusrf/domain/results.hpp"

namespace horusrf::output {

void write_csv(std::ostream& output,
               const domain::CharacterizationResult& result);

} // namespace horusrf::output
