#include "test_support.hpp"

#include <array>
#include <string_view>

#include "horusrf/parser/lexer.hpp"

using horusrf::parser::SourcePosition;
using horusrf::parser::TokenKind;
using horusrf::parser::lex;

void run_lexer_tests() {
    {
        constexpr std::string_view source =
            "characterize reference power sweep frequency step measure derive "
            "calibration correction over name abc_123";
        const auto tokens = lex(source);
        const std::array expected{
            TokenKind::Characterize, TokenKind::Reference, TokenKind::Power,
            TokenKind::Sweep, TokenKind::Frequency, TokenKind::Step,
            TokenKind::Measure, TokenKind::Derive, TokenKind::Calibration,
            TokenKind::Correction, TokenKind::Over, TokenKind::Identifier,
            TokenKind::Identifier, TokenKind::EndOfFile};
        CHECK_EQ(tokens.size(), expected.size());
        for (std::size_t index = 0; index < expected.size(); ++index)
            CHECK_EQ(tokens[index].kind, expected[index]);
    }
    {
        const auto tokens = lex("0 42 2.40 Hz kHz MHz GHz dBm dB");
        const std::array expected{
            TokenKind::Number, TokenKind::Number, TokenKind::Number,
            TokenKind::UnitHz, TokenKind::UnitKHz, TokenKind::UnitMHz,
            TokenKind::UnitGHz, TokenKind::UnitDBm, TokenKind::UnitDB,
            TokenKind::EndOfFile};
        CHECK_EQ(tokens.size(), expected.size());
        for (std::size_t index = 0; index < expected.size(); ++index)
            CHECK_EQ(tokens[index].kind, expected[index]);
        CHECK_EQ(tokens[2].lexeme, "2.40");
    }
    {
        const auto tokens = lex("{}=.. .+-(),");
        const std::array expected{
            TokenKind::LeftBrace, TokenKind::RightBrace, TokenKind::Equal,
            TokenKind::Range, TokenKind::Dot, TokenKind::Plus, TokenKind::Minus,
            TokenKind::LeftParen, TokenKind::RightParen, TokenKind::Comma,
            TokenKind::EndOfFile};
        CHECK_EQ(tokens.size(), expected.size());
        for (std::size_t index = 0; index < expected.size(); ++index)
            CHECK_EQ(tokens[index].kind, expected[index]);
    }
    {
        const auto tokens = lex("a // ignored\n  b// eof comment");
        CHECK_EQ(tokens.size(), std::size_t{3});
        CHECK_EQ(tokens[0].lexeme, "a");
        CHECK_EQ(tokens[1].lexeme, "b");
        CHECK_EQ(tokens[0].span.begin, (SourcePosition{1, 1, 0}));
        CHECK_EQ(tokens[0].span.end, (SourcePosition{1, 2, 1}));
        CHECK_EQ(tokens[1].span.begin, (SourcePosition{2, 3, 15}));
        CHECK_EQ(tokens[2].span.begin, (SourcePosition{2, 18, 30}));
        CHECK_EQ(tokens[2].span.begin, tokens[2].span.end);
    }
    {
        for (const auto malformed : {"2.", ".5", "1.2.3", "12abc", "@"}) {
            const auto tokens = lex(malformed);
            CHECK_EQ(tokens.front().kind, TokenKind::Invalid);
            CHECK_EQ(tokens.front().lexeme, std::string{malformed});
        }
    }
    {
        const auto tokens = lex("-10 2.40..2.50");
        CHECK_EQ(tokens[0].kind, TokenKind::Minus);
        CHECK_EQ(tokens[1].kind, TokenKind::Number);
        CHECK_EQ(tokens[2].kind, TokenKind::Number);
        CHECK_EQ(tokens[3].kind, TokenKind::Range);
        CHECK_EQ(tokens[4].kind, TokenKind::Number);
    }
    {
        const auto tokens = lex("Characterize hz DBM");
        CHECK_EQ(tokens[0].kind, TokenKind::Identifier);
        CHECK_EQ(tokens[1].kind, TokenKind::Identifier);
        CHECK_EQ(tokens[2].kind, TokenKind::Identifier);
    }
}
