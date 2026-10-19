#pragma once

#include <string_view>

#include "horusrf/ast/ast.hpp"

namespace horusrf::parser::antlr_frontend {

// Migration-only seam. This header is source-private and deliberately exposes
// no generated ANTLR types. The public parser API continues to use the
// handwritten frontend until the parity and cutover milestones are complete.
[[nodiscard]] ast::Program parse(std::string_view source);

} // namespace horusrf::parser::antlr_frontend
