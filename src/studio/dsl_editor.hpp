// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QFont>
#include <QPlainTextEdit>

namespace horusrf::studio {

class LineNumberArea;

class DslEditor final : public QPlainTextEdit {
public:
    explicit DslEditor(QWidget* parent = nullptr);

    int lineNumberAreaWidth() const;
    void paintLineNumberArea(QPaintEvent* event);
    void resetZoom();

protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect& rect, int dy);
    void highlightCurrentLine();

    QFont default_font_;
    LineNumberArea* line_number_area_;
};

} // namespace horusrf::studio
