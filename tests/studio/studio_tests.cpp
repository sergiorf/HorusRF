// SPDX-License-Identifier: GPL-3.0-only
#include "dsl_editor.hpp"
#include "experiment_runner.hpp"
#include "main_window.hpp"
#include "syntax_highlighter.hpp"

#include <QAction>
#include <QEventLoop>
#include <QFileInfo>
#include <QListWidget>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QtTest>

namespace {

QString runnerPath() {
    return QString::fromUtf8(HORUSRF_RUN_PATH);
}

void waitForCompletion(horusrf::studio::ExperimentRunner& runner) {
    QEventLoop loop;
    QObject::connect(&runner, &horusrf::studio::ExperimentRunner::completed,
                     &loop, &QEventLoop::quit);
    if (runner.isRunning()) loop.exec();
}

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

    void diagnosticParserHandlesWindowsPathsAndColonsInMessages() {
        const auto diagnostics = horusrf::studio::ExperimentRunner::parseDiagnostics(
            QStringLiteral("C:\\work\\buffer.hrf:12:7: semantic.bad_value: bad: value\r\n"
                           "horusrf-run: unrelated error\n"));
        QCOMPARE(diagnostics.size(), 1);
        QCOMPARE(diagnostics.front().path, QStringLiteral("C:\\work\\buffer.hrf"));
        QCOMPARE(diagnostics.front().line, 12);
        QCOMPARE(diagnostics.front().column, 7);
        QCOMPARE(diagnostics.front().code, QStringLiteral("semantic.bad_value"));
        QCOMPARE(diagnostics.front().message, QStringLiteral("bad: value"));
    }

    void runnerValidatesAndRunsTheCurrentBuffer() {
        horusrf::studio::ExperimentRunner runner(runnerPath());
        const QString invalid = QStringLiteral(
            "characterize current_buffer {\n  measure @\n}\n");
        QVERIFY(runner.validate(invalid));
        QVERIFY(!runner.run(invalid));
        waitForCompletion(runner);
        QCOMPARE(runner.lastResult().exit_code, 4);
        QCOMPARE(runner.lastResult().diagnostics.size(), 1);
        QCOMPARE(runner.lastResult().diagnostics.front().line, 2);
        QCOMPARE(runner.lastResult().diagnostics.front().code,
                 QStringLiteral("parse.invalid_token"));
        QVERIFY(runner.lastResult().standard_output.isEmpty());
        QVERIFY(!runner.lastResult().standard_error.isEmpty());

        horusrf::studio::MainWindow templateWindow(nullptr, runnerPath());
        const QString valid = templateWindow.editor()->toPlainText();
        QVERIFY(runner.validate(valid));
        waitForCompletion(runner);
        QVERIFY(runner.lastResult().succeeded());
        QVERIFY(runner.lastResult().standard_output.isEmpty());
        QVERIFY(runner.lastResult().standard_error.isEmpty());

        QVERIFY(runner.run(valid));
        waitForCompletion(runner);
        QVERIFY(runner.lastResult().succeeded());
        QVERIFY(runner.lastResult().standard_output.contains(QStringLiteral("samples: 101")));
        QVERIFY(QFileInfo::exists(runner.lastResult().csv_path));
    }

    void mainWindowPresentsAndNavigatesDiagnostics() {
        horusrf::studio::MainWindow window(nullptr, runnerPath());
        window.editor()->setPlainText(QStringLiteral(
            "characterize current_buffer {\n  measure @\n}\n"));
        auto* action = window.findChild<QAction*>(QStringLiteral("validateAction"));
        auto* list = window.findChild<QListWidget*>(QStringLiteral("diagnosticsList"));
        QVERIFY(action != nullptr);
        QVERIFY(list != nullptr);

        action->trigger();
        QVERIFY(!action->isEnabled());
        waitForCompletion(*window.runner());
        QVERIFY(action->isEnabled());
        QCOMPARE(list->count(), 1);
        QVERIFY(list->item(0)->text().contains(QStringLiteral("parse.invalid_token")));

        list->itemClicked(list->item(0));
        QCOMPARE(window.editor()->textCursor().blockNumber(), 1);
        QCOMPARE(window.editor()->textCursor().positionInBlock(), 10);
    }

    void runnerReportsMissingExecutable() {
        horusrf::studio::ExperimentRunner runner(
            QStringLiteral("Z:/missing/horusrf-run-does-not-exist.exe"));
        QVERIFY(runner.validate(QStringLiteral("characterize c {}")));
        waitForCompletion(runner);
        QVERIFY(!runner.lastResult().succeeded());
        QCOMPARE(runner.lastResult().exit_code, -1);
        QVERIFY(!runner.lastResult().standard_error.isEmpty());
    }
};

QTEST_MAIN(StudioTests)
#include "studio_tests.moc"
