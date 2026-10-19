#pragma once

#include <string_view>
#include <vector>

#include "horusrf/ast/ast.hpp"
#include "horusrf/parser/token.hpp"

namespace horusrf::parser::antlr_frontend {

// Migration-only seam. This header is source-private and deliberately exposes
// no generated ANTLR types. The public parser API continues to use the
// handwritten frontend until the parity and cutover milestones are complete.
[[nodiscard]] std::vector<Token> tokenize(std::string_view source);
[[nodiscard]] ast::Program parse(std::string_view source);

} // namespace horusrf::parser::antlr_frontend
