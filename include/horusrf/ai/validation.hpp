#pragma once

#include <iosfwd>
#include <string_view>

#include "horusrf/ai/contracts.hpp"

namespace horusrf::ai {

// Runs the same complete compiler validation as `horusrf-run --check`:
// frontend parsing/semantic analysis followed by IR lowering.
[[nodiscard]] ValidationResult validate_script(std::string_view source);

// Writes one deterministic JSON document using the
// horusrf.validation-result schema version 1.
void write_validation_json(std::ostream& output, const ValidationResult& result);

} // namespace horusrf::ai
