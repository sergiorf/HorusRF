// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>

#include <vector>

class QTextDocument;

namespace horusrf::studio {

class SyntaxHighlighter final : public QSyntaxHighlighter {
public:
    explicit SyntaxHighlighter(QTextDocument* document);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    std::vector<Rule> rules_;
    QRegularExpression comment_pattern_;
    QTextCharFormat comment_format_;
};

} // namespace horusrf::studio
