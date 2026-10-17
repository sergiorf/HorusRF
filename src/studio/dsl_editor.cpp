// SPDX-License-Identifier: GPL-3.0-only
#include "dsl_editor.hpp"

#include "syntax_highlighter.hpp"

#include <QEvent>
#include <QFontDatabase>
#include <QPainter>
#include <QTextBlock>

namespace horusrf::studio {
namespace {

QColor currentLineColor(const QPalette& palette) {
    constexpr qreal accent_weight = 0.10;
    const QColor base = palette.color(QPalette::Base);
    const QColor accent = palette.color(QPalette::Highlight);
    return QColor::fromRgbF(
        base.redF() * (1.0 - accent_weight) + accent.redF() * accent_weight,
        base.greenF() * (1.0 - accent_weight) + accent.greenF() * accent_weight,
        base.blueF() * (1.0 - accent_weight) + accent.blueF() * accent_weight);
}

} // namespace

class LineNumberArea final : public QWidget {
public:
    explicit LineNumberArea(DslEditor* editor) : QWidget(editor), editor_(editor) {}

    QSize sizeHint() const override { return {editor_->lineNumberAreaWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent* event) override {
        editor_->paintLineNumberArea(event);
    }

private:
    DslEditor* editor_;
};

DslEditor::DslEditor(QWidget* parent)
    : QPlainTextEdit(parent), line_number_area_(new LineNumberArea(this)) {
    setObjectName(QStringLiteral("dslEditor"));
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4.0);
    new SyntaxHighlighter(document());

    connect(this, &QPlainTextEdit::blockCountChanged, this,
            [this] { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this,
            [this](const QRect& rect, int dy) { updateLineNumberArea(rect, dy); });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this,
            [this] { highlightCurrentLine(); });
    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

void DslEditor::changeEvent(QEvent* event) {
    QPlainTextEdit::changeEvent(event);
    if (event->type() == QEvent::ReadOnlyChange ||
        event->type() == QEvent::PaletteChange) {
        highlightCurrentLine();
    }
}

int DslEditor::lineNumberAreaWidth() const {
    int digits = 1;
    for (int maximum = qMax(1, blockCount()); maximum >= 10; maximum /= 10) ++digits;
    return 8 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void DslEditor::updateLineNumberAreaWidth() {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void DslEditor::updateLineNumberArea(const QRect& rect, int dy) {
    if (dy != 0) {
        line_number_area_->scroll(0, dy);
    } else {
        line_number_area_->update(0, rect.y(), line_number_area_->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) updateLineNumberAreaWidth();
}

void DslEditor::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const QRect contents = contentsRect();
    line_number_area_->setGeometry(
        QRect(contents.left(), contents.top(), lineNumberAreaWidth(), contents.height()));
}

void DslEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> selections;
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(currentLineColor(palette()));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        selections.append(selection);
    }
    setExtraSelections(selections);
    line_number_area_->update();
}

void DslEditor::paintLineNumberArea(QPaintEvent* event) {
    QPainter painter(line_number_area_);
    painter.fillRect(event->rect(), palette().alternateBase());

    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const bool is_current_line = number == textCursor().blockNumber();
            QFont number_font = font();
            number_font.setBold(is_current_line);
            painter.setFont(number_font);
            painter.setPen(palette().color(is_current_line ? QPalette::Highlight
                                                           : QPalette::PlaceholderText));
            painter.drawText(0, top, line_number_area_->width() - 4,
                             fontMetrics().height(), Qt::AlignRight,
                             QString::number(number + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++number;
    }
}

} // namespace horusrf::studio
