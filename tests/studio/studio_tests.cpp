// SPDX-License-Identifier: GPL-3.0-only
#include "dsl_editor.hpp"
#include "main_window.hpp"
#include "syntax_highlighter.hpp"

#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QtTest>

namespace {

QTextCharFormat formatAt(const QTextBlock& block, int position) {
    for (const auto& range : block.layout()->formats()) {
        if (position >= range.start && position < range.start + range.length) {
            return range.format;
        }
    }
    return {};
}

} // namespace

class StudioTests final : public QObject {
    Q_OBJECT

private slots:
    void editorProvidesLineNumbersAndWritableLineHighlight() {
        horusrf::studio::DslEditor editor;
        editor.setPlainText(QStringLiteral("one\ntwo\nthree\n"));
        QVERIFY(editor.lineNumberAreaWidth() > 0);
        QCOMPARE(editor.blockCount(), 4);
        QCOMPARE(editor.extraSelections().size(), 1);

        editor.setReadOnly(true);
        QCOMPARE(editor.extraSelections().size(), 0);
    }

    void highlighterRecognizesTokensAndCommentsTakePrecedence() {
        QTextDocument document;
        horusrf::studio::SyntaxHighlighter highlighter(&document);
        const QString source = QStringLiteral(
            "characterize sample { reference power = -10 dBm // power 20 GHz");
        document.setPlainText(source);
        highlighter.rehighlight();

        const QTextBlock block = document.firstBlock();
        const auto keyword = formatAt(block, source.indexOf(QStringLiteral("characterize")));
        const auto identifier = formatAt(block, source.indexOf(QStringLiteral("sample")));
        const auto number = formatAt(block, source.indexOf(QStringLiteral("10")));
        const auto unit = formatAt(block, source.indexOf(QStringLiteral("dBm")));
        const int comment = source.indexOf(QStringLiteral("//"));

        QVERIFY(keyword.foreground().color() != identifier.foreground().color());
        QVERIFY(number.foreground().color() != identifier.foreground().color());
        QVERIFY(unit.foreground().color() != identifier.foreground().color());
        QCOMPARE(formatAt(block, comment + 3).foreground().color(),
                 formatAt(block, source.lastIndexOf(QStringLiteral("GHz"))).foreground().color());
        QVERIFY(formatAt(block, comment + 3).foreground().color() !=
                keyword.foreground().color());
    }

    void startupLoadsCanonicalTemplateUnmodified() {
        horusrf::studio::MainWindow window;
        const QString text = window.editor()->toPlainText();
        QVERIFY(text.contains(QStringLiteral("characterize tx_path")));
        QVERIFY(text.contains(QStringLiteral("derive calibration tx_power")));
        QVERIFY(window.currentFilePath().isEmpty());
        QVERIFY(!window.editor()->document()->isModified());
        QVERIFY(window.windowTitle().contains(QStringLiteral("Untitled")));
        QVERIFY(!window.isWindowModified());
    }

    void editingUpdatesModifiedStateAndTitleMarker() {
        horusrf::studio::MainWindow window;
        window.editor()->moveCursor(QTextCursor::End);
        window.editor()->insertPlainText(QStringLiteral("// edited"));
        QVERIFY(window.editor()->document()->isModified());
        QVERIFY(window.isWindowModified());
        QVERIFY(window.windowTitle().contains(QStringLiteral("[*]")));

        window.editor()->document()->setModified(false);
        QVERIFY(!window.isWindowModified());
    }
};

QTEST_MAIN(StudioTests)
#include "studio_tests.moc"
