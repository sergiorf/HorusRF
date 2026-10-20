#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "horusrf/parser/token.hpp"

namespace horusrf::parser {

enum class FixedTokenCategory { Keyword, Unit, Operator, Punctuation };

struct FixedTokenDefinition {
    TokenKind kind{TokenKind::Invalid};
    FixedTokenCategory category{FixedTokenCategory::Punctuation};
    std::string spelling;
};

[[nodiscard]] std::vector<Token> lex(std::string_view source);
[[nodiscard]] const std::vector<FixedTokenDefinition>& fixed_token_definitions();
[[nodiscard]] const std::vector<std::string>& grammar_rule_names();

} // namespace horusrf::parser
