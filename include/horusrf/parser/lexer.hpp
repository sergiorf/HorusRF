#pragma once

#include <string_view>
#include <vector>

#include "horusrf/parser/token.hpp"

namespace horusrf::parser {

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source) {}

    [[nodiscard]] std::vector<Token> tokenize();

private:
    [[nodiscard]] bool at_end() const noexcept;
    [[nodiscard]] char peek(std::size_t lookahead = 0) const noexcept;
    char advance() noexcept;
    void skip_ignored() noexcept;
    [[nodiscard]] Token scan_word(SourcePosition begin);
    [[nodiscard]] Token scan_number(SourcePosition begin);
    [[nodiscard]] Token make_token(TokenKind kind, SourcePosition begin,
                                   std::size_t begin_offset) const;

    std::string_view source_;
    SourcePosition position_{};
};

[[nodiscard]] std::vector<Token> lex(std::string_view source);

} // namespace horusrf::parser
