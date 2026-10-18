// SPDX-License-Identifier: GPL-3.0-only
#include "syntax_highlighter.hpp"

#include <QColor>
#include <QFont>
#include <QRegularExpression>
#include <QTextDocument>

namespace horusrf::studio {
namespace {

QTextCharFormat colored(const QColor& color, bool bold = false) {
    QTextCharFormat format;
    format.setForeground(color);
    if (bold) format.setFontWeight(QFont::Bold);
    return format;
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
    rules_.push_back({QRegularExpression{QStringLiteral("(?:<=|>=|==|!=|[+\\-<>])")}, operation});
    rules_.push_back({QRegularExpression{QStringLiteral(
                          "\\b(?:characterize|reference|power|sweep|frequency|temperature|step|"
                          "measure|derive|calibration|correction|over|assert|evm|frequency_error)\\b")},
                      keyword});
    rules_.push_back({QRegularExpression{
                          QStringLiteral("(?:\\b(?:Hz|kHz|MHz|GHz|dBm|dB|K|degC|deltaK|deltaC)\\b|%)")},
                      unit});
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
