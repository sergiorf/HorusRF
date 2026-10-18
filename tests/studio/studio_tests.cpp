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
#include <QComboBox>
#include <QEventLoop>
#include <QFileInfo>
#include <QLabel>
#include <QLineSeries>
#include <QListWidget>
#include <QPen>
#include <QTableWidget>
#include <QTabWidget>
#include <QTreeWidget>
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

QString qualificationSource() {
    return QStringLiteral(
        "characterize studio_qualification {\n"
        "  reference power = -10 dBm\n"
        "  sweep frequency 2.40 GHz .. 2.50 GHz step 25 MHz\n"
        "  sweep temperature -20 degC .. 80 degC step 20 deltaC\n"
        "  measure output_power = power\n"
        "  measure evm_result = evm\n"
        "  measure carrier_error = frequency_error\n"
        "  derive power_error = output_power - reference.power\n"
        "  assert abs(power_error) <= 1.5 dB\n"
        "  assert evm_result <= 2 %\n"
        "  assert abs(carrier_error) <= 20 kHz\n"
        "  derive calibration tx_power {\n"
        "    correction = -power_error\n"
        "    over frequency, temperature\n"
        "  }\n"
        "}\n");
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

        QPalette palette = editor.palette();
        const QColor base{24, 26, 28};
        const QColor accent{44, 124, 204};
        palette.setColor(QPalette::Base, base);
        palette.setColor(QPalette::Highlight, accent);
        editor.setPalette(palette);
        const QColor currentLine =
            editor.extraSelections().front().format.background().color();
        QVERIFY(currentLine != base);
        QVERIFY(currentLine != accent);
        QVERIFY(qAbs(currentLine.red() - base.red()) <
                qAbs(accent.red() - base.red()));
        QVERIFY(qAbs(currentLine.green() - base.green()) <
                qAbs(accent.green() - base.green()));
        QVERIFY(qAbs(currentLine.blue() - base.blue()) <
                qAbs(accent.blue() - base.blue()));

        editor.setReadOnly(true);
        QCOMPARE(editor.extraSelections().size(), 0);
    }

    void highlighterRecognizesTokensAndCommentsTakePrecedence() {
        QTextDocument document;
        horusrf::studio::SyntaxHighlighter highlighter(&document);
        const QString source = QStringLiteral(
            "characterize sample { measure output_power = power "
            "assert abs(output_power) <= 1 dB // power 20 GHz");
        document.setPlainText(source);
        highlighter.rehighlight();

        const QTextBlock block = document.firstBlock();
        const auto keyword = formatAt(block, source.indexOf(QStringLiteral("characterize")));
        const auto identifier = formatAt(block, source.indexOf(QStringLiteral("sample")));
        const auto measurement =
            formatAt(block, source.indexOf(QStringLiteral("output_power")));
        const auto measurement_reference =
            formatAt(block, source.lastIndexOf(QStringLiteral("output_power")));
        const auto number = formatAt(block, source.indexOf(QStringLiteral("1 dB")));
        const auto unit = formatAt(block, source.indexOf(QStringLiteral("dB")));
        const auto measurement_keyword =
            formatAt(block, source.indexOf(QStringLiteral("measure")));
        const auto assertion = formatAt(block, source.indexOf(QStringLiteral("assert")));
        const auto comparison = formatAt(block, source.indexOf(QStringLiteral("<=")));
        const int comment = source.indexOf(QStringLiteral("//"));

        QCOMPARE(identifier.foreground().style(), Qt::NoBrush);
        QCOMPARE(measurement.foreground().style(), Qt::NoBrush);
        QCOMPARE(measurement_reference.foreground().style(), Qt::NoBrush);
        QVERIFY(keyword.foreground().color() != identifier.foreground().color());
        QVERIFY(number.foreground().color() != identifier.foreground().color());
        QVERIFY(unit.foreground().color() != identifier.foreground().color());
        QCOMPARE(measurement_keyword.foreground().color(), keyword.foreground().color());
        QCOMPARE(assertion.foreground().color(), keyword.foreground().color());
        QVERIFY(comparison.foreground().color() != identifier.foreground().color());
        QCOMPARE(formatAt(block, comment + 3).foreground().color(),
                 formatAt(block, source.lastIndexOf(QStringLiteral("GHz"))).foreground().color());
        QVERIFY(formatAt(block, comment + 3).foreground().color() !=
                keyword.foreground().color());
    }

    void viewMenuControlsEditorZoom() {
        horusrf::studio::MainWindow window;
        auto* zoomIn = window.findChild<QAction*>(QStringLiteral("zoomInAction"));
        auto* zoomOut = window.findChild<QAction*>(QStringLiteral("zoomOutAction"));
        auto* resetZoom =
            window.findChild<QAction*>(QStringLiteral("resetZoomAction"));

        QVERIFY(zoomIn != nullptr);
        QVERIFY(zoomOut != nullptr);
        QVERIFY(resetZoom != nullptr);
        QCOMPARE(zoomIn->shortcut(), QKeySequence(QKeySequence::ZoomIn));
        QCOMPARE(zoomOut->shortcut(), QKeySequence(QKeySequence::ZoomOut));
        QCOMPARE(resetZoom->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_0));

        const qreal initialSize = window.editor()->font().pointSizeF();
        zoomIn->trigger();
        QVERIFY(window.editor()->font().pointSizeF() > initialSize);
        resetZoom->trigger();
        QCOMPARE(window.editor()->font().pointSizeF(), initialSize);
        zoomOut->trigger();
        QVERIFY(window.editor()->font().pointSizeF() < initialSize);
        resetZoom->trigger();
        QCOMPARE(window.editor()->font().pointSizeF(), initialSize);
    }

    void startupLoadsShowcaseTemplateUnmodified() {
        horusrf::studio::MainWindow window;
        const QString text = window.editor()->toPlainText();
        QVERIFY(text.contains(QStringLiteral("characterize tx_qualification")));
        QVERIFY(text.contains(QStringLiteral("sweep temperature")));
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

    void statusBarTracksOneBasedCursorLineAndColumn() {
        horusrf::studio::MainWindow window;
        auto* position =
            window.findChild<QLabel*>(QStringLiteral("cursorPositionLabel"));
        QVERIFY(position != nullptr);

        window.editor()->setPlainText(QStringLiteral("first\nsecond"));
        QTextCursor cursor(window.editor()->document()->findBlockByNumber(1));
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 3);
        window.editor()->setTextCursor(cursor);
        QCOMPARE(position->text(), QStringLiteral("Ln 2, Col 4"));
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
        QVERIFY(runner.lastResult().producedResults());
        QVERIFY(runner.lastResult().standard_output.contains(QStringLiteral("samples: 126")));
        QVERIFY(QFileInfo::exists(runner.lastResult().json_path));

        QString error;
        const auto model = horusrf::studio::ResultsModel::fromJsonFile(
            runner.lastResult().json_path, &error);
        QVERIFY2(model.has_value(), qPrintable(error));
        QCOMPARE(model->characterization(), QStringLiteral("tx_qualification"));
        QCOMPARE(model->points().size(), 126);
        QCOMPARE(model->qualification().assertions.size(), 3);
        QCOMPARE(model->calibrations().size(), 1);
        QCOMPARE(model->calibrations().front().entries.size(), 126);
        for (const auto& point : model->points()) {
            QCOMPARE(point.measurements.size(), 3);
            QCOMPARE(point.assertions.size(), 3);
        }

        horusrf::studio::ResultsView view;
        view.setResults(*model, runner.lastResult().standard_output);
        auto* tabs = view.findChild<QTabWidget*>(QStringLiteral("resultTabs"));
        auto* heatmap = view.findChild<QTableWidget*>(QStringLiteral("heatmapTable"));
        auto* heatmapX = view.findChild<QComboBox*>(QStringLiteral("heatmapX"));
        auto* heatmapY = view.findChild<QComboBox*>(QStringLiteral("heatmapY"));
        auto* heatmapScalar = view.findChild<QComboBox*>(QStringLiteral("heatmapScalar"));
        QVERIFY(tabs != nullptr);
        QVERIFY(heatmap != nullptr);
        QVERIFY(heatmapX != nullptr);
        QVERIFY(heatmapY != nullptr);
        QVERIFY(heatmapScalar != nullptr);
        QCOMPARE(tabs->currentWidget()->objectName(), QStringLiteral("heatmapPage"));
        QCOMPARE(heatmapX->currentData().toString(), QStringLiteral("frequency"));
        QCOMPARE(heatmapY->currentData().toString(), QStringLiteral("temperature"));
        QCOMPARE(heatmapScalar->currentData().toString(), QStringLiteral("power_error"));
        QCOMPARE(heatmap->columnCount(), 21);
        QCOMPARE(heatmap->rowCount(), 6);
    }

    void examplesMenuLoadsEveryRegisteredResourceAsUntitledDocument() {
        horusrf::studio::MainWindow window;
        const QList<QPair<QString, QString>> examples{
            {QStringLiteral("basicTxCalibrationExampleAction"),
             QStringLiteral(":/examples/tx_path_characterization.hrf")},
            {QStringLiteral("txQualificationExampleAction"),
             QStringLiteral(":/examples/qualified_measurements.hrf")},
            {QStringLiteral("temperatureCharacterizationExampleAction"),
             QStringLiteral(":/examples/multidimensional_check.hrf")},
            {QStringLiteral("namedRfMeasurementsExampleAction"),
             QStringLiteral(":/examples/named_measurements.hrf")},
            {QStringLiteral("completeStudioShowcaseExampleAction"),
             QStringLiteral(":/examples/studio_default.hrf")},
        };

        for (const auto& [objectName, resource] : examples) {
            auto* action = window.findChild<QAction*>(objectName);
            QVERIFY2(action != nullptr, qPrintable(objectName));
            QCOMPARE(action->data().toString(), resource);
            action->trigger();
            QVERIFY(!window.editor()->toPlainText().isEmpty());
            QVERIFY(window.editor()->toPlainText().contains(QStringLiteral("characterize ")));
            QVERIFY(window.currentFilePath().isEmpty());
            QVERIFY(!window.editor()->document()->isModified());
            QVERIFY(window.windowTitle().contains(QStringLiteral("Untitled")));
        }
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

    void structuredResultsDriveFiltersPlotsHeatmapsAndQualificationNavigation() {
        horusrf::studio::ExperimentRunner runner(runnerPath());
        QVERIFY(runner.run(qualificationSource()));
        waitForCompletion(runner);
        QCOMPARE(runner.lastResult().exit_code, 6);
        QVERIFY(runner.lastResult().producedResults());

        QString error;
        const auto model = horusrf::studio::ResultsModel::fromJsonFile(
            runner.lastResult().json_path, &error);
        QVERIFY2(model.has_value(), qPrintable(error));
        QVERIFY(model->isStructured());
        QCOMPARE(model->characterization(), QStringLiteral("studio_qualification"));
        QCOMPARE(model->points().size(), 30);
        QCOMPARE(model->points().front().coordinates.size(), 2);
        QCOMPARE(model->points().front().measurements.size(), 3);
        QCOMPARE(model->qualification().status, QStringLiteral("fail"));
        QCOMPARE(model->qualification().failed_points, 12);
        QCOMPARE(model->calibrations().front().entries.size(), 30);

        horusrf::studio::ResultsView view;
        view.setResults(*model, runner.lastResult().standard_output);
        QCOMPARE(view.sampleCount(), 30);
        auto* table = view.findChild<QTableWidget*>(QStringLiteral("pointTable"));
        auto* qualificationFilter =
            view.findChild<QComboBox*>(QStringLiteral("qualificationFilter"));
        auto* selectedChart =
            view.findChild<QChartView*>(QStringLiteral("selectedChart"));
        auto* heatmap = view.findChild<QTableWidget*>(QStringLiteral("heatmapTable"));
        auto* heatmapX = view.findChild<QComboBox*>(QStringLiteral("heatmapX"));
        auto* heatmapY = view.findChild<QComboBox*>(QStringLiteral("heatmapY"));
        auto* heatmapScalar = view.findChild<QComboBox*>(QStringLiteral("heatmapScalar"));
        auto* dashboard =
            view.findChild<QLabel*>(QStringLiteral("qualificationDashboard"));
        auto* summary = view.findChild<QLabel*>(QStringLiteral("resultSummary"));
        auto* failures = view.findChild<QTreeWidget*>(QStringLiteral("failureTree"));
        auto* calibration =
            view.findChild<QTableWidget*>(QStringLiteral("calibrationTable"));
        QVERIFY(table != nullptr);
        QVERIFY(qualificationFilter != nullptr);
        QVERIFY(selectedChart != nullptr);
        auto* plot = selectedChart->chart()->findChild<QLineSeries*>(
            QStringLiteral("selectedSeries"));
        QVERIFY(plot != nullptr);
        QVERIFY(heatmap != nullptr);
        QVERIFY(heatmapX != nullptr);
        QVERIFY(heatmapY != nullptr);
        QVERIFY(heatmapScalar != nullptr);
        QVERIFY(dashboard != nullptr);
        QVERIFY(summary != nullptr);
        QVERIFY(failures != nullptr);
        QVERIFY(calibration != nullptr);
        QCOMPARE(table->rowCount(), 30);
        QCOMPARE(table->columnCount(), 7);
        QCOMPARE(plot->count(), 5);
        QCOMPARE(heatmap->columnCount(), 5);
        QCOMPARE(heatmap->rowCount(), 6);
        auto* tabs = view.findChild<QTabWidget*>(QStringLiteral("resultTabs"));
        QVERIFY(tabs != nullptr);
        QCOMPARE(tabs->currentWidget()->objectName(), QStringLiteral("heatmapPage"));
        QCOMPARE(heatmapX->currentData().toString(), QStringLiteral("frequency"));
        QCOMPARE(heatmapY->currentData().toString(), QStringLiteral("temperature"));
        QCOMPARE(heatmapScalar->currentData().toString(), QStringLiteral("power_error"));
        QCOMPARE(calibration->rowCount(), 30);
        QVERIFY(dashboard->text().contains(QStringLiteral("60.0%")));
        QVERIFY(summary->text().contains(QStringLiteral("studio_qualification — 30 points")));
        QVERIFY(summary->text().contains(
            QStringLiteral("Sweep dimensions: frequency, temperature")));
        QVERIFY(summary->text().contains(QStringLiteral("output_power (power)")));
        QVERIFY(summary->text().contains(QStringLiteral("evm_result (evm)")));
        QVERIFY(summary->text().contains(
            QStringLiteral("carrier_error (frequency_error)")));
        QVERIFY(summary->text().contains(QStringLiteral("Qualification: failed")));
        QVERIFY(summary->text().contains(QStringLiteral("Calibration surfaces: tx_power")));
        QCOMPARE(failures->topLevelItemCount(), 3);
        QCOMPARE(failures->topLevelItem(1)->childCount(), 12);

        auto* plotAxis = view.findChild<QComboBox*>(QStringLiteral("plotAxis"));
        QVERIFY(plotAxis != nullptr);
        plotAxis->setCurrentIndex(1);
        QCOMPARE(plot->count(), 6);

        qualificationFilter->setCurrentIndex(2);
        QCOMPARE(table->rowCount(), 12);

        heatmap->itemClicked(heatmap->item(0, 0));
        QCOMPARE(tabs->currentIndex(), 0);
        QCOMPARE(table->selectedItems().front()->data(Qt::UserRole).toInt(), 0);
        auto* temperatureFilter =
            view.findChild<QComboBox*>(QStringLiteral("coordinateFilter_temperature"));
        QVERIFY(temperatureFilter != nullptr);
        temperatureFilter->setCurrentIndex(1);
        QCOMPARE(table->rowCount(), 5);

        QSignalSpy navigation(&view,
            &horusrf::studio::ResultsView::sourceNavigationRequested);
        failures->itemActivated(failures->topLevelItem(1)->child(0), 0);
        QCOMPARE(navigation.count(), 1);
        QCOMPARE(navigation.front().at(0).toInt(), 10);

        auto* plotScalar = view.findChild<QComboBox*>(QStringLiteral("plotScalar"));
        auto* fixedFrequency =
            view.findChild<QComboBox*>(QStringLiteral("plotFixed_frequency"));
        QVERIFY(plotScalar != nullptr);
        QVERIFY(fixedFrequency != nullptr);
        plotAxis->setCurrentIndex(plotAxis->findData(QStringLiteral("temperature")));
        plotScalar->setCurrentIndex(plotScalar->findData(QStringLiteral("carrier_error")));
        fixedFrequency->setCurrentIndex(2);
        heatmapX->setCurrentIndex(heatmapX->findData(QStringLiteral("temperature")));
        heatmapY->setCurrentIndex(heatmapY->findData(QStringLiteral("frequency")));
        heatmapScalar->setCurrentIndex(
            heatmapScalar->findData(QStringLiteral("output_power")));
        tabs->setCurrentWidget(view.findChild<QWidget*>(QStringLiteral("plotPage")));
        const QVariant fixedFrequencyValue = fixedFrequency->currentData();

        view.setResults(*model, runner.lastResult().standard_output);
        QCOMPARE(tabs->currentWidget()->objectName(), QStringLiteral("plotPage"));
        QCOMPARE(plotAxis->currentData().toString(), QStringLiteral("temperature"));
        QCOMPARE(plotScalar->currentData().toString(), QStringLiteral("carrier_error"));
        fixedFrequency = view.findChild<QComboBox*>(QStringLiteral("plotFixed_frequency"));
        QVERIFY(fixedFrequency != nullptr);
        QCOMPARE(fixedFrequency->currentData(), fixedFrequencyValue);
        QCOMPARE(heatmapX->currentData().toString(), QStringLiteral("temperature"));
        QCOMPARE(heatmapY->currentData().toString(), QStringLiteral("frequency"));
        QCOMPARE(heatmapScalar->currentData().toString(), QStringLiteral("output_power"));

        plotAxis->setCurrentIndex(plotAxis->findData(QStringLiteral("frequency")));
        auto* fixedTemperature =
            view.findChild<QComboBox*>(QStringLiteral("plotFixed_temperature"));
        QVERIFY(fixedTemperature != nullptr);
        fixedTemperature->setCurrentIndex(fixedTemperature->count() - 1);
        const QVariant removedTemperature = fixedTemperature->currentData();

        QString reducedCoordinatesSource = qualificationSource();
        reducedCoordinatesSource.replace(QStringLiteral(".. 80 degC"),
                                         QStringLiteral(".. 60 degC"));
        QVERIFY(runner.run(reducedCoordinatesSource));
        waitForCompletion(runner);
        QVERIFY(runner.lastResult().producedResults());
        const auto reducedCoordinatesModel =
            horusrf::studio::ResultsModel::fromJsonFile(
                runner.lastResult().json_path, &error);
        QVERIFY2(reducedCoordinatesModel.has_value(), qPrintable(error));
        view.setResults(*reducedCoordinatesModel, runner.lastResult().standard_output);
        QCOMPARE(tabs->currentWidget()->objectName(), QStringLiteral("plotPage"));
        QCOMPARE(plotAxis->currentData().toString(), QStringLiteral("frequency"));
        QCOMPARE(plotScalar->currentData().toString(), QStringLiteral("carrier_error"));
        fixedTemperature =
            view.findChild<QComboBox*>(QStringLiteral("plotFixed_temperature"));
        QVERIFY(fixedTemperature != nullptr);
        QVERIFY(fixedTemperature->currentData() != removedTemperature);
        QCOMPARE(fixedTemperature->currentIndex(), 1);
        QCOMPARE(heatmapX->currentData().toString(), QStringLiteral("temperature"));
        QCOMPARE(heatmapY->currentData().toString(), QStringLiteral("frequency"));
        QCOMPARE(heatmapScalar->currentData().toString(), QStringLiteral("output_power"));

        QString oneDimensionalSource = qualificationSource();
        oneDimensionalSource.remove(
            QStringLiteral("  sweep temperature -20 degC .. 80 degC step 20 deltaC\n"));
        oneDimensionalSource.replace(QStringLiteral("over frequency, temperature"),
                                     QStringLiteral("over frequency"));
        QVERIFY(runner.run(oneDimensionalSource));
        waitForCompletion(runner);
        QVERIFY(runner.lastResult().producedResults());
        const auto oneDimensionalModel = horusrf::studio::ResultsModel::fromJsonFile(
            runner.lastResult().json_path, &error);
        QVERIFY2(oneDimensionalModel.has_value(), qPrintable(error));
        view.setResults(*oneDimensionalModel, runner.lastResult().standard_output);
        QCOMPARE(tabs->currentWidget()->objectName(), QStringLiteral("plotPage"));
        QCOMPARE(plotAxis->currentData().toString(), QStringLiteral("frequency"));
        QVERIFY(!tabs->isTabEnabled(
            tabs->indexOf(view.findChild<QWidget*>(QStringLiteral("heatmapPage")))));

        QString assertionFreeSource = qualificationSource();
        assertionFreeSource.remove(QStringLiteral("  assert abs(power_error) <= 1.5 dB\n"));
        assertionFreeSource.remove(QStringLiteral("  assert evm_result <= 2 %\n"));
        assertionFreeSource.remove(
            QStringLiteral("  assert abs(carrier_error) <= 20 kHz\n"));
        QVERIFY(runner.run(assertionFreeSource));
        waitForCompletion(runner);
        QVERIFY(runner.lastResult().producedResults());
        const auto assertionFreeModel = horusrf::studio::ResultsModel::fromJsonFile(
            runner.lastResult().json_path, &error);
        QVERIFY2(assertionFreeModel.has_value(), qPrintable(error));
        view.setResults(*assertionFreeModel, runner.lastResult().standard_output);
        QVERIFY(summary->text().contains(
            QStringLiteral("Qualification: not evaluated (no assertions)")));

        QString passingSource = qualificationSource();
        passingSource.replace(QStringLiteral("<= 1.5 dB"), QStringLiteral("<= 100 dB"));
        passingSource.replace(QStringLiteral("<= 2 %"), QStringLiteral("<= 100 %"));
        passingSource.replace(QStringLiteral("<= 20 kHz"), QStringLiteral("<= 100 MHz"));
        QVERIFY(runner.run(passingSource));
        waitForCompletion(runner);
        QCOMPARE(runner.lastResult().exit_code, 0);
        const auto passingModel = horusrf::studio::ResultsModel::fromJsonFile(
            runner.lastResult().json_path, &error);
        QVERIFY2(passingModel.has_value(), qPrintable(error));
        view.setResults(*passingModel, runner.lastResult().standard_output);
        QVERIFY(summary->text().contains(QStringLiteral("Qualification: passed")));
        auto* status = view.findChild<QLabel*>(QStringLiteral("resultsStatus"));
        QVERIFY(status != nullptr);
        QCOMPARE(status->text(), QStringLiteral("Experiment completed."));
    }

    void structuredResultsRejectMalformedAndUnsupportedJson() {
        QString error;
        QVERIFY(!horusrf::studio::ResultsModel::fromJson(QByteArray("{"), &error));
        QVERIFY(!error.isEmpty());
        error.clear();
        QVERIFY(!horusrf::studio::ResultsModel::fromJson(
            QByteArray(R"({"schema":{"name":"horusrf.characterization-result","version":2}})"),
            &error));
        QVERIFY(error.contains(QStringLiteral("version")));
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
        auto* metrics = view.findChild<QLabel*>(QStringLiteral("resultMetrics"));
        auto* overlap = view.findChild<QLabel*>(QStringLiteral("overlapNotice"));
        auto* summary = view.findChild<QLabel*>(QStringLiteral("resultSummary"));
        auto* correctionChart = view.findChild<QChartView*>(
            QStringLiteral("correctionChart"));
        QVERIFY(measured != nullptr);
        QVERIFY(corrected != nullptr);
        QVERIFY(reference != nullptr);
        QVERIFY(frequency != nullptr);
        QVERIFY(power != nullptr);
        QVERIFY(toggle != nullptr);
        QVERIFY(count != nullptr);
        QVERIFY(metrics != nullptr);
        QVERIFY(overlap != nullptr);
        QVERIFY(summary != nullptr);
        QVERIFY(correctionChart != nullptr);
        auto* correction = correctionChart->chart()->findChild<QLineSeries*>(
            QStringLiteral("correctionSeries"));
        auto* correctionAxis = correctionChart->chart()->findChild<QValueAxis*>(
            QStringLiteral("correctionAxis"));
        QVERIFY(correction != nullptr);
        QVERIFY(correctionAxis != nullptr);
        QCOMPARE(measured->count(), 2);
        QCOMPARE(corrected->count(), 2);
        QCOMPARE(reference->count(), 2);
        QCOMPARE(measured->at(0).x(), 2.4);
        QCOMPARE(frequency->titleText(), QStringLiteral("Frequency (GHz)"));
        QCOMPARE(power->titleText(), QStringLiteral("Power (dBm)"));
        QVERIFY(frequency->min() < frequency->max());
        QVERIFY(power->min() < -10.25);
        QVERIFY(power->max() > -9.5);
        QCOMPARE(measured->pen().color(), QColor(QStringLiteral("#2e7d32")));
        QCOMPARE(measured->pen().style(), Qt::SolidLine);
        QCOMPARE(reference->pen().color(), QColor(QStringLiteral("#1565c0")));
        QCOMPARE(reference->pen().style(), Qt::DashLine);
        QCOMPARE(corrected->pen().color(), QColor(QStringLiteral("#ef6c00")));
        QCOMPARE(corrected->pen().style(), Qt::SolidLine);
        QCOMPARE(correction->count(), 2);
        QCOMPARE(correction->at(0).y(), -0.5);
        QCOMPARE(correctionAxis->titleText(), QStringLiteral("Correction (dB)"));
        QVERIFY(correctionAxis->min() < -0.5);
        QVERIFY(correctionAxis->max() > 0.25);
        QVERIFY(!overlap->isHidden());
        QVERIFY(overlap->text().contains(QStringLiteral("overlap exactly")));
        QVERIFY(metrics->text().contains(QStringLiteral("Calibration fit")));
        QVERIFY(metrics->text().contains(QStringLiteral("0.400 dB")));
        QVERIFY(metrics->text().contains(QStringLiteral("N/A (perfect in-sample fit)")));
        QVERIFY(metrics->text().contains(QStringLiteral("Validation metrics")));
        QVERIFY(!metrics->text().contains(QStringLiteral("inf"), Qt::CaseInsensitive));
        QVERIFY(count->text().contains(QStringLiteral("2")));
        QVERIFY(summary->isHidden());

        toggle->setChecked(false);
        QVERIFY(!reference->isVisible());
        QVERIFY(measured->isVisible());
        QVERIFY(overlap->isHidden());

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
        QVERIFY(correctionAxis->min() < 0.0);
        QVERIFY(correctionAxis->max() > 0.0);
    }

    void powerBoundsTrackVisibleSeries() {
        const QByteArray csv(
            "frequency_hz,reference_power_dbm,measured_power_dbm,error_db,"
            "correction_db,corrected_power_dbm,residual_error_db\n"
            "2400000000,-20,-9,11,-1,-10,10\n"
            "2500000000,-20,-9,11,-1,-10,10\n");
        const auto model = horusrf::studio::ResultsModel::fromCsv(csv);
        QVERIFY(model.has_value());
        horusrf::studio::ResultsView view;
        view.setResults(*model, {});
        auto* chartView = view.findChild<QChartView*>(QStringLiteral("resultsChart"));
        auto* toggle = view.findChild<QCheckBox*>(QStringLiteral("showReference"));
        QVERIFY(chartView != nullptr);
        auto* power = chartView->chart()->findChild<QValueAxis*>(
            QStringLiteral("powerAxis"));
        QVERIFY(power != nullptr);
        QVERIFY(toggle != nullptr);
        QVERIFY(power->min() < -20.0);
        toggle->setChecked(false);
        QVERIFY(power->min() < -10.0);
        QVERIFY(power->min() > -11.0);
        QVERIFY(power->max() > -9.0);
    }

    void sixHundredOneSampleCalibrationRendersWithPaddedAxesAndFormattedMetrics() {
        QByteArray csv(
            "frequency_hz,reference_power_dbm,measured_power_dbm,error_db,"
            "correction_db,corrected_power_dbm,residual_error_db\n");
        for (int index = 0; index <= 600; ++index) {
            const double measured = -9.65 + (index % 2 == 0 ? 0.05 : -0.05);
            const double correction = -10.0 - measured;
            csv += QByteArray::number(2400000000LL + 1000000LL * index) + ",-10,";
            csv += QByteArray::number(measured, 'g', 15) + ',';
            csv += QByteArray::number(measured + 10.0, 'g', 15) + ',';
            csv += QByteArray::number(correction, 'g', 15) + ",-10,0\n";
        }
        const auto model = horusrf::studio::ResultsModel::fromCsv(csv);
        QVERIFY(model.has_value());
        horusrf::studio::ResultsView view;
        view.setResults(
            *model,
            QStringLiteral("uncorrected_rms_error_db: 0.35355339059327379\n"
                           "corrected_rms_error_db: 0\n"
                           "maximum_absolute_error_db: 0.40000000000000036\n"
                           "corrected_maximum_absolute_error_db: 0\n"
                           "rms_improvement_ratio: inf\n"));

        QCOMPARE(view.sampleCount(), 601);
        auto* chartView = view.findChild<QChartView*>(QStringLiteral("resultsChart"));
        auto* correctionChart = view.findChild<QChartView*>(
            QStringLiteral("correctionChart"));
        auto* metrics = view.findChild<QLabel*>(QStringLiteral("resultMetrics"));
        QVERIFY(chartView != nullptr);
        QVERIFY(correctionChart != nullptr);
        auto* measured = chartView->chart()->findChild<QLineSeries*>(
            QStringLiteral("measuredSeries"));
        auto* reference = chartView->chart()->findChild<QLineSeries*>(
            QStringLiteral("referenceSeries"));
        auto* corrected = chartView->chart()->findChild<QLineSeries*>(
            QStringLiteral("correctedSeries"));
        auto* correction = correctionChart->chart()->findChild<QLineSeries*>(
            QStringLiteral("correctionSeries"));
        auto* frequency = chartView->chart()->findChild<QValueAxis*>(
            QStringLiteral("frequencyAxis"));
        auto* power = chartView->chart()->findChild<QValueAxis*>(
            QStringLiteral("powerAxis"));
        QVERIFY(measured != nullptr);
        QVERIFY(reference != nullptr);
        QVERIFY(corrected != nullptr);
        QVERIFY(correction != nullptr);
        QVERIFY(frequency != nullptr);
        QVERIFY(power != nullptr);
        QVERIFY(metrics != nullptr);
        QCOMPARE(measured->count(), 601);
        QCOMPARE(reference->count(), 601);
        QCOMPARE(corrected->count(), 601);
        QCOMPARE(correction->count(), 601);
        QVERIFY(frequency->min() < 2.4);
        QVERIFY(frequency->max() > 3.0);
        QVERIFY(power->min() < -10.0);
        QVERIFY(power->max() > -9.6);
        QVERIFY(metrics->text().contains(QStringLiteral("0.354 dB")));
        QVERIFY(metrics->text().contains(QStringLiteral("0.400 dB")));
        QVERIFY(metrics->text().contains(QStringLiteral("N/A (perfect in-sample fit)")));
        QVERIFY(!metrics->text().contains(QStringLiteral("0.400000000")));
        QVERIFY(!metrics->text().contains(QStringLiteral("inf"), Qt::CaseInsensitive));
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
        QCOMPARE(view->sampleCount(), 126);
        auto* summary = view->findChild<QLabel*>(QStringLiteral("resultSummary"));
        auto* status = view->findChild<QLabel*>(QStringLiteral("resultsStatus"));
        QVERIFY(summary != nullptr);
        QVERIFY(status != nullptr);
        const QString successfulSummary = summary->text();
        QVERIFY(successfulSummary.contains(QStringLiteral("tx_qualification — 126 points")));

        window.editor()->setPlainText(QStringLiteral(
            "characterize current_buffer {\n  measure @\n}\n"));
        run->trigger();
        waitForCompletion(*window.runner());
        QVERIFY(!window.runner()->lastResult().succeeded());
        QVERIFY(view->hasResults());
        QCOMPARE(view->sampleCount(), 126);
        QCOMPARE(summary->text(), successfulSummary);
        QVERIFY(status->text().contains(QStringLiteral("failed with exit code")));
    }

    void mainWindowDisplaysQualificationFailureAndNavigatesToAssertion() {
        horusrf::studio::MainWindow window(nullptr, runnerPath());
        window.editor()->setPlainText(qualificationSource());
        auto* run = window.findChild<QAction*>(QStringLiteral("runAction"));
        auto* view = window.findChild<horusrf::studio::ResultsView*>(
            QStringLiteral("resultsView"));
        QVERIFY(run != nullptr);
        QVERIFY(view != nullptr);
        run->trigger();
        waitForCompletion(*window.runner());
        QCOMPARE(window.runner()->lastResult().exit_code, 6);
        QVERIFY(view->hasResults());
        QCOMPARE(view->sampleCount(), 30);

        auto* failures = view->findChild<QTreeWidget*>(QStringLiteral("failureTree"));
        QVERIFY(failures != nullptr);
        QVERIFY(failures->topLevelItem(1)->childCount() > 0);
        failures->itemActivated(failures->topLevelItem(1)->child(0), 0);
        QCOMPARE(window.editor()->textCursor().blockNumber(), 9);
        QCOMPARE(window.editor()->textCursor().positionInBlock(), 9);
    }
};

QTEST_MAIN(StudioTests)
#include "studio_tests.moc"
