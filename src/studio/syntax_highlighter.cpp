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
    const auto identifier = colored(QColor{36, 41, 47});
    const auto keyword = colored(QColor{130, 80, 223}, true);
    const auto number = colored(QColor{9, 105, 218});
    const auto unit = colored(QColor{5, 125, 88}, true);
    const auto operation = colored(QColor{207, 34, 46});

    // General tokens are applied first; more specific tokens override them.
    rules_.push_back({QRegularExpression{QStringLiteral("\\b[A-Za-z][A-Za-z0-9_]*\\b")},
                      identifier});
    rules_.push_back({QRegularExpression{QStringLiteral("\\b\\d+(?:\\.\\d+)?\\b")},
                      number});
    rules_.push_back({QRegularExpression{QStringLiteral("[+\\-]")}, operation});
    rules_.push_back({QRegularExpression{QStringLiteral(
                          "\\b(?:characterize|reference|power|sweep|frequency|step|"
                          "measure|derive|calibration|correction|over)\\b")},
                      keyword});
    rules_.push_back({QRegularExpression{
                          QStringLiteral("\\b(?:Hz|kHz|MHz|GHz|dBm|dB)\\b")},
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
