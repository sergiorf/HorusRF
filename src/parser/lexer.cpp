#include "horusrf/parser/lexer.hpp"

#include <cctype>
#include <string>
#include <unordered_map>

namespace horusrf::parser {
namespace {

bool is_ascii_letter(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool is_ascii_digit(char c) noexcept { return c >= '0' && c <= '9'; }
bool is_word_tail(char c) noexcept { return is_ascii_letter(c) || is_ascii_digit(c) || c == '_'; }

TokenKind word_kind(std::string_view word) {
    static const std::unordered_map<std::string_view, TokenKind> words{
        {"characterize", TokenKind::Characterize}, {"reference", TokenKind::Reference},
        {"power", TokenKind::Power}, {"sweep", TokenKind::Sweep},
        {"frequency", TokenKind::Frequency}, {"step", TokenKind::Step},
        {"measure", TokenKind::Measure}, {"derive", TokenKind::Derive},
        {"calibration", TokenKind::Calibration}, {"correction", TokenKind::Correction},
        {"over", TokenKind::Over}, {"Hz", TokenKind::UnitHz},
        {"kHz", TokenKind::UnitKHz}, {"MHz", TokenKind::UnitMHz},
        {"GHz", TokenKind::UnitGHz}, {"dBm", TokenKind::UnitDBm},
        {"dB", TokenKind::UnitDB},
    };
    const auto found = words.find(word);
    return found == words.end() ? TokenKind::Identifier : found->second;
}

} // namespace

std::string_view token_kind_name(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::EndOfFile: return "end of file";
    case TokenKind::Identifier: return "identifier";
    case TokenKind::Number: return "number";
    case TokenKind::Characterize: return "characterize";
    case TokenKind::Reference: return "reference";
    case TokenKind::Power: return "power";
    case TokenKind::Sweep: return "sweep";
    case TokenKind::Frequency: return "frequency";
    case TokenKind::Step: return "step";
    case TokenKind::Measure: return "measure";
    case TokenKind::Derive: return "derive";
    case TokenKind::Calibration: return "calibration";
    case TokenKind::Correction: return "correction";
    case TokenKind::Over: return "over";
    case TokenKind::LeftBrace: return "{";
    case TokenKind::RightBrace: return "}";
    case TokenKind::Equal: return "=";
    case TokenKind::Range: return "..";
    case TokenKind::Dot: return ".";
    case TokenKind::Plus: return "+";
    case TokenKind::Minus: return "-";
    case TokenKind::LeftParen: return "(";
    case TokenKind::RightParen: return ")";
    case TokenKind::Comma: return ",";
    case TokenKind::UnitHz: return "Hz";
    case TokenKind::UnitKHz: return "kHz";
    case TokenKind::UnitMHz: return "MHz";
    case TokenKind::UnitGHz: return "GHz";
    case TokenKind::UnitDBm: return "dBm";
    case TokenKind::UnitDB: return "dB";
    case TokenKind::Invalid: return "invalid token";
    }
    return "unknown token";
}

bool is_unit(TokenKind kind) noexcept {
    return kind == TokenKind::UnitHz || kind == TokenKind::UnitKHz ||
           kind == TokenKind::UnitMHz || kind == TokenKind::UnitGHz ||
           kind == TokenKind::UnitDBm || kind == TokenKind::UnitDB;
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
        case '=': kind = TokenKind::Equal; break;
        case '+': kind = TokenKind::Plus; break;
        case '-': kind = TokenKind::Minus; break;
        case '(': kind = TokenKind::LeftParen; break;
        case ')': kind = TokenKind::RightParen; break;
        case ',': kind = TokenKind::Comma; break;
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
