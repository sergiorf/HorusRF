// SPDX-License-Identifier: GPL-3.0-only
#include "dsl_editor.hpp"
#include "experiment_runner.hpp"
#include "main_window.hpp"
#include "results_model.hpp"
#include "results_view.hpp"
#include "syntax_highlighter.hpp"

#include <QAction>
#include <QChart>
#include <QChartView>
#include <QCheckBox>
#include <QEventLoop>
#include <QFileInfo>
#include <QLabel>
#include <QLineSeries>
#include <QListWidget>
#include <QValueAxis>
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

    void resultsParserAcceptsExactFiniteSevenColumnCsv() {
        const QByteArray csv(
            "frequency_hz,reference_power_dbm,measured_power_dbm,error_db,"
            "correction_db,corrected_power_dbm,residual_error_db\r\n"
            "2400000000,-10,-9.5,0.5,-0.5,-10,0\r\n"
            "2500000000,-10,-10.25,-0.25,0.25,-10,0\r\n");
        QString error;
        const auto result = horusrf::studio::ResultsModel::fromCsv(csv, &error);
        QVERIFY2(result.has_value(), qPrintable(error));
        QCOMPARE(result->samples().size(), 2);
        QCOMPARE(result->samples().front().frequency_hz, 2.4e9);
        QCOMPARE(result->samples().front().measured_power_dbm, -9.5);
        QCOMPARE(result->samples().back().correction_db, 0.25);
    }

    void resultsParserRejectsMalformedAndEmptyData() {
        constexpr auto header =
            "frequency_hz,reference_power_dbm,measured_power_dbm,error_db,"
            "correction_db,corrected_power_dbm,residual_error_db\n";
        const QList<QByteArray> invalidCsvs{
            QByteArray("wrong,header\n1,2,3,4,5,6,7\n"),
            QByteArray(header),
            QByteArray(header) + "1,2,3,4,5,6\n",
            QByteArray(header) + "1,2,,4,5,6,7\n",
            QByteArray(header) + "1,2,nan,4,5,6,7\n",
            QByteArray(header) + "1,2,inf,4,5,6,7\n",
            QByteArray(header) + "1,2,3,4,5,6,7,8\n",
        };
        for (const QByteArray& csv : invalidCsvs) {
            QString error;
            QVERIFY(!horusrf::studio::ResultsModel::fromCsv(csv, &error).has_value());
            QVERIFY(!error.isEmpty());
        }
    }

    void resultsViewConfiguresSeriesAxesAndReferenceToggle() {
        const QByteArray csv(
            "frequency_hz,reference_power_dbm,measured_power_dbm,error_db,"
            "correction_db,corrected_power_dbm,residual_error_db\n"
            "2400000000,-10,-9.5,0.5,-0.5,-10,0\n"
            "2500000000,-10,-10.25,-0.25,0.25,-10,0\n");
        const auto model = horusrf::studio::ResultsModel::fromCsv(csv);
        QVERIFY(model.has_value());

        horusrf::studio::ResultsView view;
        view.setResults(*model, QStringLiteral(
            "samples: 2\nuncorrected_rms_error_db: 0.4\n"
            "corrected_rms_error_db: 0\n"));
        QVERIFY(view.hasResults());
        QCOMPARE(view.sampleCount(), 2);

        auto* chartView = view.findChild<QChartView*>(QStringLiteral("resultsChart"));
        QVERIFY(chartView != nullptr);
        auto* measured = chartView->chart()->findChild<QLineSeries*>(
            QStringLiteral("measuredSeries"));
        auto* corrected = chartView->chart()->findChild<QLineSeries*>(
            QStringLiteral("correctedSeries"));
        auto* reference = chartView->chart()->findChild<QLineSeries*>(
            QStringLiteral("referenceSeries"));
        auto* frequency = chartView->chart()->findChild<QValueAxis*>(
            QStringLiteral("frequencyAxis"));
        auto* power = chartView->chart()->findChild<QValueAxis*>(
            QStringLiteral("powerAxis"));
        auto* toggle = view.findChild<QCheckBox*>(QStringLiteral("showReference"));
        auto* count = view.findChild<QLabel*>(QStringLiteral("sampleCount"));
        QVERIFY(measured != nullptr);
        QVERIFY(corrected != nullptr);
        QVERIFY(reference != nullptr);
        QVERIFY(frequency != nullptr);
        QVERIFY(power != nullptr);
        QVERIFY(toggle != nullptr);
        QVERIFY(count != nullptr);
        QCOMPARE(measured->count(), 2);
        QCOMPARE(corrected->count(), 2);
        QCOMPARE(reference->count(), 2);
        QCOMPARE(measured->at(0).x(), 2.4);
        QCOMPARE(frequency->titleText(), QStringLiteral("Frequency (GHz)"));
        QCOMPARE(power->titleText(), QStringLiteral("Power (dBm)"));
        QVERIFY(frequency->min() < frequency->max());
        QVERIFY(power->min() < power->max());
        QVERIFY(count->text().contains(QStringLiteral("2")));

        toggle->setChecked(false);
        QVERIFY(!reference->isVisible());
        QVERIFY(measured->isVisible());

        const QByteArray singleSampleCsv(
            "frequency_hz,reference_power_dbm,measured_power_dbm,error_db,"
            "correction_db,corrected_power_dbm,residual_error_db\n"
            "2400000000,-10,-10,0,0,-10,0\n");
        const auto singleSample =
            horusrf::studio::ResultsModel::fromCsv(singleSampleCsv);
        QVERIFY(singleSample.has_value());
        view.setResults(*singleSample, {});
        QCOMPARE(measured->count(), 1);
        QVERIFY(frequency->min() < 2.4);
        QVERIFY(frequency->max() > 2.4);
        QVERIFY(power->min() < -10.0);
        QVERIFY(power->max() > -10.0);
    }

    void mainWindowRetainsSuccessfulResultsAfterLaterFailure() {
        horusrf::studio::MainWindow window(nullptr, runnerPath());
        auto* run = window.findChild<QAction*>(QStringLiteral("runAction"));
        auto* view = window.findChild<horusrf::studio::ResultsView*>(
            QStringLiteral("resultsView"));
        QVERIFY(run != nullptr);
        QVERIFY(view != nullptr);

        run->trigger();
        waitForCompletion(*window.runner());
        QVERIFY(view->hasResults());
        QCOMPARE(view->sampleCount(), 101);

        window.editor()->setPlainText(QStringLiteral(
            "characterize current_buffer {\n  measure @\n}\n"));
        run->trigger();
        waitForCompletion(*window.runner());
        QVERIFY(!window.runner()->lastResult().succeeded());
        QVERIFY(view->hasResults());
        QCOMPARE(view->sampleCount(), 101);
    }
};

QTEST_MAIN(StudioTests)
#include "studio_tests.moc"
