#pragma once

#include <string_view>
#include <vector>

#include "horusrf/parser/token.hpp"

namespace horusrf::parser {

[[nodiscard]] std::vector<Token> lex(std::string_view source);

} // namespace horusrf::parser
