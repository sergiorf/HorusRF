#include "horusrf/parser/lexer.hpp"

#include <array>
#include <cctype>
#include <string>

namespace horusrf::parser {
namespace {

bool is_ascii_letter(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool is_ascii_digit(char c) noexcept { return c >= '0' && c <= '9'; }
bool is_word_tail(char c) noexcept { return is_ascii_letter(c) || is_ascii_digit(c) || c == '_'; }

struct WordToken {
    std::string_view spelling;
    TokenKind kind;
    bool unit;
};

constexpr std::array word_tokens{
    WordToken{"characterize", TokenKind::Characterize, false},
    WordToken{"reference", TokenKind::Reference, false},
    WordToken{"power", TokenKind::Power, false},
    WordToken{"sweep", TokenKind::Sweep, false},
    WordToken{"frequency", TokenKind::Frequency, false},
    WordToken{"temperature", TokenKind::Temperature, false},
    WordToken{"step", TokenKind::Step, false},
    WordToken{"measure", TokenKind::Measure, false},
    WordToken{"derive", TokenKind::Derive, false},
    WordToken{"calibration", TokenKind::Calibration, false},
    WordToken{"correction", TokenKind::Correction, false},
    WordToken{"over", TokenKind::Over, false},
    WordToken{"assert", TokenKind::Assert, false},
    WordToken{"Hz", TokenKind::UnitHz, true},
    WordToken{"kHz", TokenKind::UnitKHz, true},
    WordToken{"MHz", TokenKind::UnitMHz, true},
    WordToken{"GHz", TokenKind::UnitGHz, true},
    WordToken{"dBm", TokenKind::UnitDBm, true},
    WordToken{"dB", TokenKind::UnitDB, true},
    WordToken{"K", TokenKind::UnitK, true},
    WordToken{"degC", TokenKind::UnitDegC, true},
    WordToken{"deltaK", TokenKind::UnitDeltaK, true},
    WordToken{"deltaC", TokenKind::UnitDeltaC, true},
};

TokenKind word_kind(std::string_view word) noexcept {
    for (const auto& token : word_tokens) {
        if (token.spelling == word) return token.kind;
    }
    return TokenKind::Identifier;
}

} // namespace

std::string_view token_kind_name(TokenKind kind) noexcept {
    for (const auto& token : word_tokens) {
        if (token.kind == kind) return token.spelling;
    }
    switch (kind) {
    case TokenKind::EndOfFile: return "end of file";
    case TokenKind::Identifier: return "identifier";
    case TokenKind::Number: return "number";
    case TokenKind::LeftBrace: return "{";
    case TokenKind::RightBrace: return "}";
    case TokenKind::Equal: return "=";
    case TokenKind::EqualEqual: return "==";
    case TokenKind::NotEqual: return "!=";
    case TokenKind::Less: return "<";
    case TokenKind::LessEqual: return "<=";
    case TokenKind::Greater: return ">";
    case TokenKind::GreaterEqual: return ">=";
    case TokenKind::Range: return "..";
    case TokenKind::Dot: return ".";
    case TokenKind::Plus: return "+";
    case TokenKind::Minus: return "-";
    case TokenKind::LeftParen: return "(";
    case TokenKind::RightParen: return ")";
    case TokenKind::Comma: return ",";
    case TokenKind::UnitPercent: return "%";
    case TokenKind::Invalid: return "invalid token";
    case TokenKind::Characterize:
    case TokenKind::Reference:
    case TokenKind::Power:
    case TokenKind::Sweep:
    case TokenKind::Frequency:
    case TokenKind::Temperature:
    case TokenKind::Step:
    case TokenKind::Measure:
    case TokenKind::Derive:
    case TokenKind::Calibration:
    case TokenKind::Correction:
    case TokenKind::Over:
    case TokenKind::Assert:
    case TokenKind::UnitHz:
    case TokenKind::UnitKHz:
    case TokenKind::UnitMHz:
    case TokenKind::UnitGHz:
    case TokenKind::UnitDBm:
    case TokenKind::UnitDB:
    case TokenKind::UnitK:
    case TokenKind::UnitDegC:
    case TokenKind::UnitDeltaK:
    case TokenKind::UnitDeltaC: break;
    }
    return "unknown token";
}

bool is_unit(TokenKind kind) noexcept {
    if (kind == TokenKind::UnitPercent) return true;
    for (const auto& token : word_tokens) {
        if (token.kind == kind) return token.unit;
    }
    return false;
}

bool Lexer::at_end() const noexcept { return position_.offset >= source_.size(); }

char Lexer::peek(std::size_t lookahead) const noexcept {
    const auto offset = position_.offset + lookahead;
    return offset < source_.size() ? source_[offset] : '\0';
}

char Lexer::advance() noexcept {
    const char c = source_[position_.offset++];
    if (c == '\n') {
        ++position_.line;
        position_.column = 1;
    } else {
        ++position_.column;
    }
    return c;
}

void Lexer::skip_ignored() noexcept {
    for (;;) {
        while (!at_end() && std::isspace(static_cast<unsigned char>(peek())) != 0) {
            advance();
        }
        if (peek() != '/' || peek(1) != '/') {
            return;
        }
        while (!at_end() && peek() != '\n') {
            advance();
        }
    }
}

Token Lexer::make_token(TokenKind kind, SourcePosition begin, std::size_t begin_offset) const {
    return Token{kind, std::string(source_.substr(begin_offset, position_.offset - begin_offset)),
                 SourceSpan{begin, position_}};
}

Token Lexer::scan_word(SourcePosition begin) {
    const auto start = begin.offset;
    while (is_word_tail(peek())) {
        advance();
    }
    const auto text = source_.substr(start, position_.offset - start);
    return make_token(word_kind(text), begin, start);
}

Token Lexer::scan_number(SourcePosition begin) {
    const auto start = begin.offset;
    while (is_ascii_digit(peek())) {
        advance();
    }

    bool malformed = false;
    if (peek() == '.' && peek(1) != '.') {
        advance();
        if (!is_ascii_digit(peek())) {
            malformed = true;
        }
        while (is_ascii_digit(peek())) {
            advance();
        }
    }
    if (is_ascii_letter(peek()) || peek() == '_' ||
        (peek() == '.' && peek(1) != '.')) {
        malformed = true;
        while (is_word_tail(peek()) || peek() == '.') {
            advance();
        }
    }
    return make_token(malformed ? TokenKind::Invalid : TokenKind::Number, begin, start);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        skip_ignored();
        const SourcePosition begin = position_;
        const auto start = position_.offset;
        if (at_end()) {
            tokens.push_back(Token{TokenKind::EndOfFile, {}, SourceSpan{begin, begin}});
            return tokens;
        }
        const char c = advance();
        if (is_ascii_letter(c)) {
            tokens.push_back(scan_word(begin));
            continue;
        }
        if (is_ascii_digit(c)) {
            tokens.push_back(scan_number(begin));
            continue;
        }

        TokenKind kind = TokenKind::Invalid;
        switch (c) {
        case '{': kind = TokenKind::LeftBrace; break;
        case '}': kind = TokenKind::RightBrace; break;
        case '=':
            if (peek() == '=') { advance(); kind = TokenKind::EqualEqual; }
            else kind = TokenKind::Equal;
            break;
        case '!':
            if (peek() == '=') { advance(); kind = TokenKind::NotEqual; }
            break;
        case '<':
            if (peek() == '=') { advance(); kind = TokenKind::LessEqual; }
            else kind = TokenKind::Less;
            break;
        case '>':
            if (peek() == '=') { advance(); kind = TokenKind::GreaterEqual; }
            else kind = TokenKind::Greater;
            break;
        case '+': kind = TokenKind::Plus; break;
        case '-': kind = TokenKind::Minus; break;
        case '(': kind = TokenKind::LeftParen; break;
        case ')': kind = TokenKind::RightParen; break;
        case ',': kind = TokenKind::Comma; break;
        case '%': kind = TokenKind::UnitPercent; break;
        case '.':
            if (peek() == '.') {
                advance();
                kind = TokenKind::Range;
            } else if (is_ascii_digit(peek())) {
                while (is_ascii_digit(peek())) {
                    advance();
                }
                kind = TokenKind::Invalid;
            } else {
                kind = TokenKind::Dot;
            }
            break;
        default: break;
        }
        tokens.push_back(make_token(kind, begin, start));
    }
}

std::vector<Token> lex(std::string_view source) { return Lexer(source).tokenize(); }

} // namespace horusrf::parser
