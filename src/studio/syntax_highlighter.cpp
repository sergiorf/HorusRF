// SPDX-License-Identifier: GPL-3.0-only
#include "syntax_highlighter.hpp"

#include <QColor>
#include <QFont>
#include <QRegularExpression>
#include <QTextDocument>

#include <algorithm>

#include "horusrf/parser/lexer.hpp"

namespace horusrf::studio {
namespace {

QTextCharFormat colored(const QColor& color, bool bold = false) {
    QTextCharFormat format;
    format.setForeground(color);
    if (bold) format.setFontWeight(QFont::Bold);
    return format;
}

QRegularExpression fixedTokenPattern(horusrf::parser::FixedTokenCategory category) {
    QStringList words;
    QStringList symbols;
    for (const auto& token : horusrf::parser::fixed_token_definitions()) {
        if (token.category != category) continue;
        const auto spelling = QString::fromStdString(token.spelling);
        const auto escaped = QRegularExpression::escape(spelling);
        if (!spelling.isEmpty() && spelling.front().isLetter()) words.push_back(escaped);
        else symbols.push_back(escaped);
    }

    auto longestFirst = [](const QString& left, const QString& right) {
        return left.size() > right.size();
    };
    std::sort(words.begin(), words.end(), longestFirst);
    std::sort(symbols.begin(), symbols.end(), longestFirst);

    QStringList alternatives;
    if (!words.isEmpty()) {
        alternatives.push_back(QStringLiteral("(?<![A-Za-z0-9_])(?:%1)(?![A-Za-z0-9_])")
                                   .arg(words.join(QLatin1Char('|'))));
    }
    if (!symbols.isEmpty()) {
        alternatives.push_back(QStringLiteral("(?:%1)").arg(symbols.join(QLatin1Char('|'))));
    }
    return QRegularExpression{alternatives.join(QLatin1Char('|'))};
}

} // namespace

SyntaxHighlighter::SyntaxHighlighter(QTextDocument* document)
    : QSyntaxHighlighter(document),
      comment_pattern_(QStringLiteral("//[^\\n]*")),
      comment_format_(colored(QColor{106, 115, 125})) {
    const auto keyword = colored(QColor{130, 80, 223}, true);
    const auto number = colored(QColor{9, 105, 218});
    const auto unit = colored(QColor{5, 125, 88}, true);
    const auto operation = colored(QColor{207, 34, 46});

    // Leave ordinary identifiers unformatted so they inherit the editor palette's
    // text color. A fixed dark identifier color becomes unreadable in dark themes.
    rules_.push_back({QRegularExpression{QStringLiteral("\\b\\d+(?:\\.\\d+)?\\b")},
                      number});
    rules_.push_back(
        {fixedTokenPattern(parser::FixedTokenCategory::Operator), operation});
    rules_.push_back(
        {fixedTokenPattern(parser::FixedTokenCategory::Keyword), keyword});
    rules_.push_back(
        {fixedTokenPattern(parser::FixedTokenCategory::Unit), unit});
}

void SyntaxHighlighter::highlightBlock(const QString& text) {
    for (const auto& rule : rules_) {
        auto matches = rule.pattern.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    auto comments = comment_pattern_.globalMatch(text);
    while (comments.hasNext()) {
        const auto match = comments.next();
        setFormat(match.capturedStart(), match.capturedLength(), comment_format_);
    }
}

} // namespace horusrf::studio
