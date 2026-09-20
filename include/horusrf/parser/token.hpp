#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace horusrf::parser {

struct SourcePosition {
    std::size_t line{1};
    std::size_t column{1};
    std::size_t offset{0};

    friend bool operator==(const SourcePosition&, const SourcePosition&) = default;
};

struct SourceSpan {
    SourcePosition begin{};
    SourcePosition end{};

    friend bool operator==(const SourceSpan&, const SourceSpan&) = default;
};

enum class TokenKind {
    EndOfFile, Identifier, Number,
    Characterize, Reference, Power, Sweep, Frequency, Step, Measure, Derive,
    Calibration, Correction, Over,
    LeftBrace, RightBrace, Equal, Range, Dot, Plus, Minus, LeftParen, RightParen,
    Comma,
    UnitHz, UnitKHz, UnitMHz, UnitGHz, UnitDBm, UnitDB,
    Invalid
};

struct Token {
    TokenKind kind{TokenKind::Invalid};
    std::string lexeme;
    SourceSpan span{};
};

[[nodiscard]] std::string_view token_kind_name(TokenKind kind) noexcept;
[[nodiscard]] bool is_unit(TokenKind kind) noexcept;

} // namespace horusrf::parser
