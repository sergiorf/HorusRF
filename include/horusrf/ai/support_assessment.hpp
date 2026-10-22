#pragma once

#include <string_view>

#include "horusrf/ai/contracts.hpp"
#include "horusrf/frontend/language_services.hpp"

namespace horusrf::ai {

// Recognizes only explicit, high-confidence authoring operations. Compiler-owned
// capability metadata remains the authority for whether a recognized operation
// is supported.
[[nodiscard]] SupportAssessment assess_instruction_support(
    std::string_view instruction,
    const frontend::LanguageCapabilities& capabilities =
        frontend::get_language_capabilities());

} // namespace horusrf::ai
