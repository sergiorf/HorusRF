// SPDX-License-Identifier: GPL-3.0-only
#include "results_view.hpp"

#include <QChart>
#include <QChartView>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineSeries>
#include <QPen>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QTabWidget>
#include <QToolTip>
#include <QTreeWidget>
#include <QValueAxis>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace horusrf::studio {
namespace {

struct AxisRange {
    double minimum;
    double maximum;
};

AxisRange paddedRange(double minimum, double maximum, double minimumSpan) {
    const double span = maximum - minimum;
    if (std::isfinite(span) && span > 0.0) {
        constexpr double paddingFraction = 0.08;
        const double padding = span * paddingFraction;
        return {minimum - padding, maximum + padding};
    }
    const double padding = minimumSpan / 2.0;
    return {minimum - padding, maximum + padding};
}

std::optional<double> reportValue(const QString& report, const QString& key) {
    const QString prefix = key + QStringLiteral(":");
    for (const QString& line : report.split('\n', Qt::SkipEmptyParts)) {
        if (!line.startsWith(prefix)) continue;
        const QString text = line.mid(prefix.size()).trimmed();
        if (text.compare(QStringLiteral("inf"), Qt::CaseInsensitive) == 0) {
            return std::numeric_limits<double>::infinity();
        }
        bool ok = false;
        const double value = text.toDouble(&ok);
        if (ok) return value;
    }
    return std::nullopt;
}

QString formatDb(double value) {
    if (value == 0.0) value = 0.0; // Avoid rendering negative zero.
    const double magnitude = std::abs(value);
    const char format = magnitude != 0.0 && magnitude < 0.0005 ? 'g' : 'f';
    return QStringLiteral("%1 dB").arg(value, 0, format, 3);
}

QString formatRatio(double value) {
    const double magnitude = std::abs(value);
    const char format = magnitude >= 10000.0 || (magnitude != 0.0 && magnitude < 0.01)
        ? 'g'
        : 'f';
    const int precision = format == 'g' ? 4 : 2;
    return QStringLiteral("%1x").arg(value, 0, format, precision);
}

QString metricText(const QString& report) {
    const auto uncorrected = reportValue(report, QStringLiteral("uncorrected_rms_error_db"));
    const auto corrected = reportValue(report, QStringLiteral("corrected_rms_error_db"));
    const auto maximum = reportValue(report, QStringLiteral("maximum_absolute_error_db"));
    const auto correctedMaximum =
        reportValue(report, QStringLiteral("corrected_maximum_absolute_error_db"));
    const auto improvement = reportValue(report, QStringLiteral("rms_improvement_ratio"));

    QStringList lines{QObject::tr("Calibration fit (same samples used to derive the correction)")};
    if (uncorrected) lines.append(QObject::tr("Uncorrected RMS error: %1").arg(formatDb(*uncorrected)));
    if (corrected) lines.append(QObject::tr("Corrected RMS error: %1").arg(formatDb(*corrected)));
    if (maximum) lines.append(QObject::tr("Maximum absolute error: %1").arg(formatDb(*maximum)));
    if (correctedMaximum) {
        lines.append(QObject::tr("Corrected maximum absolute error: %1")
                         .arg(formatDb(*correctedMaximum)));
    }
    if (corrected && *corrected == 0.0) {
        const QString explanation = uncorrected && *uncorrected != 0.0
            ? QObject::tr("N/A (perfect in-sample fit)")
            : QObject::tr("N/A (no initial error)");
        lines.append(QObject::tr("RMS improvement: %1").arg(explanation));
    } else if (improvement && std::isfinite(*improvement)) {
        lines.append(QObject::tr("RMS improvement: %1").arg(formatRatio(*improvement)));
    } else if (improvement) {
        lines.append(QObject::tr("RMS improvement: N/A"));
    }
    lines.append(QObject::tr("Validation metrics: Not available (no independent validation data)"));
    return lines.join('\n');
}

void installTooltip(QLineSeries* series, const QString& unit) {
    QObject::connect(series, &QLineSeries::hovered, series,
                     [series, unit](const QPointF& point, bool state) {
        if (!state) {
            QToolTip::hideText();
            return;
        }
        QToolTip::showText(
            QCursor::pos(),
            QStringLiteral("%1\n%2 GHz\n%3 %4")
                .arg(series->name())
                .arg(point.x(), 0, 'f', 3)
                .arg(point.y(), 0, 'f', 3)
                .arg(unit));
    });
}

} // namespace

ResultsView::ResultsView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("resultsView"));
    status_ = new QLabel(tr("Run an experiment to visualize its results."), this);
    status_->setObjectName(QStringLiteral("resultsStatus"));
    status_->setWordWrap(true);

    sample_count_ = new QLabel(this);
    sample_count_->setObjectName(QStringLiteral("sampleCount"));
    result_summary_ = new QLabel(this);
    result_summary_->setObjectName(QStringLiteral("resultSummary"));
    result_summary_->setWordWrap(true);
    result_summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    result_summary_->setVisible(false);
    metrics_ = new QLabel(this);
    metrics_->setObjectName(QStringLiteral("resultMetrics"));
    metrics_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    overlap_notice_ = new QLabel(this);
    overlap_notice_->setObjectName(QStringLiteral("overlapNotice"));
    overlap_notice_->setWordWrap(true);
    overlap_notice_->setVisible(false);

    show_reference_ = new QCheckBox(tr("Show reference"), this);
    show_reference_->setObjectName(QStringLiteral("showReference"));
    show_reference_->setChecked(true);

    reference_series_ = new QLineSeries(this);
    reference_series_->setName(tr("Reference"));
    reference_series_->setObjectName(QStringLiteral("referenceSeries"));
    measured_series_ = new QLineSeries(this);
    measured_series_->setName(tr("Measured"));
    measured_series_->setObjectName(QStringLiteral("measuredSeries"));
    corrected_series_ = new QLineSeries(this);
    corrected_series_->setName(tr("Corrected"));
    corrected_series_->setObjectName(QStringLiteral("correctedSeries"));
    correction_series_ = new QLineSeries(this);
    correction_series_->setName(tr("Correction"));
    correction_series_->setObjectName(QStringLiteral("correctionSeries"));

    QPen measuredPen(QColor(QStringLiteral("#2e7d32")), 2.0);
    QPen referencePen(QColor(QStringLiteral("#1565c0")), 2.0, Qt::DashLine);
    QPen correctedPen(QColor(QStringLiteral("#ef6c00")), 2.0);
    QPen correctionPen(QColor(QStringLiteral("#7b1fa2")), 2.0);
    measured_series_->setPen(measuredPen);
    reference_series_->setPen(referencePen);
    corrected_series_->setPen(correctedPen);
    correction_series_->setPen(correctionPen);
    installTooltip(reference_series_, QStringLiteral("dBm"));
    installTooltip(measured_series_, QStringLiteral("dBm"));
    installTooltip(corrected_series_, QStringLiteral("dBm"));
    installTooltip(correction_series_, QStringLiteral("dB"));

    auto* chart = new QChart;
    chart->setTitle(tr("Power over frequency"));
    // Draw the dashed reference last so an exactly overlapping orange corrected
    // line remains visible through the gaps.
    chart->addSeries(corrected_series_);
    chart->addSeries(measured_series_);
    chart->addSeries(reference_series_);
    chart->legend()->setVisible(true);

    frequency_axis_ = new QValueAxis(chart);
    frequency_axis_->setObjectName(QStringLiteral("frequencyAxis"));
    frequency_axis_->setTitleText(tr("Frequency (GHz)"));
    frequency_axis_->setLabelFormat(QStringLiteral("%.4g"));
    power_axis_ = new QValueAxis(chart);
    power_axis_->setObjectName(QStringLiteral("powerAxis"));
    power_axis_->setTitleText(tr("Power (dBm)"));
    power_axis_->setLabelFormat(QStringLiteral("%.4g"));
    chart->addAxis(frequency_axis_, Qt::AlignBottom);
    chart->addAxis(power_axis_, Qt::AlignLeft);
    for (QLineSeries* series : {reference_series_, measured_series_, corrected_series_}) {
        series->attachAxis(frequency_axis_);
        series->attachAxis(power_axis_);
    }

    auto* chartView = new QChartView(chart, this);
    chartView->setObjectName(QStringLiteral("resultsChart"));
    chartView->setRenderHint(QPainter::Antialiasing);

    auto* correctionChart = new QChart;
    correctionChart->setTitle(tr("Calibration correction over frequency"));
    correctionChart->addSeries(correction_series_);
    correctionChart->legend()->setVisible(true);
    correction_frequency_axis_ = new QValueAxis(correctionChart);
    correction_frequency_axis_->setObjectName(QStringLiteral("correctionFrequencyAxis"));
    correction_frequency_axis_->setTitleText(tr("Frequency (GHz)"));
    correction_frequency_axis_->setLabelFormat(QStringLiteral("%.4g"));
    correction_axis_ = new QValueAxis(correctionChart);
    correction_axis_->setObjectName(QStringLiteral("correctionAxis"));
    correction_axis_->setTitleText(tr("Correction (dB)"));
    correction_axis_->setLabelFormat(QStringLiteral("%.4g"));
    correctionChart->addAxis(correction_frequency_axis_, Qt::AlignBottom);
    correctionChart->addAxis(correction_axis_, Qt::AlignLeft);
    correction_series_->attachAxis(correction_frequency_axis_);
    correction_series_->attachAxis(correction_axis_);
    auto* correctionChartView = new QChartView(correctionChart, this);
    correctionChartView->setObjectName(QStringLiteral("correctionChart"));
    correctionChartView->setRenderHint(QPainter::Antialiasing);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("resultTabs"));

    auto* tablePage = new QWidget(tabs_);
    tablePage->setObjectName(QStringLiteral("pointsPage"));
    auto* tableLayout = new QVBoxLayout(tablePage);
    auto* filters = new QWidget(tablePage);
    coordinate_filter_layout_ = new QFormLayout(filters);
    qualification_filter_ = new QComboBox(filters);
    qualification_filter_->setObjectName(QStringLiteral("qualificationFilter"));
    qualification_filter_->addItems({tr("All points"), tr("Pass"), tr("Fail"),
                                     tr("Not evaluated")});
    coordinate_filter_layout_->addRow(tr("Qualification"), qualification_filter_);
    point_table_ = new QTableWidget(tablePage);
    point_table_->setObjectName(QStringLiteral("pointTable"));
    point_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    point_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    point_table_->horizontalHeader()->setStretchLastSection(true);
    tableLayout->addWidget(filters);
    tableLayout->addWidget(point_table_, 1);
    tabs_->addTab(tablePage, tr("Points"));

    auto* plotPage = new QWidget(tabs_);
    plotPage->setObjectName(QStringLiteral("plotPage"));
    auto* plotLayout = new QVBoxLayout(plotPage);
    auto* plotSelectors = new QWidget(plotPage);
    plot_fixed_layout_ = new QFormLayout(plotSelectors);
    plot_axis_ = new QComboBox(plotSelectors);
    plot_axis_->setObjectName(QStringLiteral("plotAxis"));
    plot_scalar_ = new QComboBox(plotSelectors);
    plot_scalar_->setObjectName(QStringLiteral("plotScalar"));
    plot_fixed_layout_->addRow(tr("Sweep axis"), plot_axis_);
    plot_fixed_layout_->addRow(tr("Value"), plot_scalar_);
    auto* selectedChart = new QChart;
    selectedChart->setTitle(tr("Selected result"));
    selected_series_ = new QLineSeries(selectedChart);
    selected_series_->setObjectName(QStringLiteral("selectedSeries"));
    selectedChart->addSeries(selected_series_);
    selected_x_axis_ = new QValueAxis(selectedChart);
    selected_y_axis_ = new QValueAxis(selectedChart);
    selected_x_axis_->setObjectName(QStringLiteral("selectedXAxis"));
    selected_y_axis_->setObjectName(QStringLiteral("selectedYAxis"));
    selectedChart->addAxis(selected_x_axis_, Qt::AlignBottom);
    selectedChart->addAxis(selected_y_axis_, Qt::AlignLeft);
    selected_series_->attachAxis(selected_x_axis_);
    selected_series_->attachAxis(selected_y_axis_);
    auto* selectedChartView = new QChartView(selectedChart, plotPage);
    selectedChartView->setObjectName(QStringLiteral("selectedChart"));
    selectedChartView->setRenderHint(QPainter::Antialiasing);
    plotLayout->addWidget(plotSelectors);
    plotLayout->addWidget(selectedChartView, 1);
    tabs_->addTab(plotPage, tr("Plot"));

    auto* heatmapPage = new QWidget(tabs_);
    heatmapPage->setObjectName(QStringLiteral("heatmapPage"));
    auto* heatmapLayout = new QVBoxLayout(heatmapPage);
    auto* heatmapSelectors = new QHBoxLayout;
    heatmap_x_ = new QComboBox(heatmapPage);
    heatmap_x_->setObjectName(QStringLiteral("heatmapX"));
    heatmap_y_ = new QComboBox(heatmapPage);
    heatmap_y_->setObjectName(QStringLiteral("heatmapY"));
    heatmap_scalar_ = new QComboBox(heatmapPage);
    heatmap_scalar_->setObjectName(QStringLiteral("heatmapScalar"));
    heatmapSelectors->addWidget(new QLabel(tr("X:"), heatmapPage));
    heatmapSelectors->addWidget(heatmap_x_);
    heatmapSelectors->addWidget(new QLabel(tr("Y:"), heatmapPage));
    heatmapSelectors->addWidget(heatmap_y_);
    heatmapSelectors->addWidget(new QLabel(tr("Value:"), heatmapPage));
    heatmapSelectors->addWidget(heatmap_scalar_);
    heatmap_ = new QTableWidget(heatmapPage);
    heatmap_->setObjectName(QStringLiteral("heatmapTable"));
    heatmap_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    heatmapLayout->addLayout(heatmapSelectors);
    heatmapLayout->addWidget(heatmap_, 1);
    tabs_->addTab(heatmapPage, tr("Heatmap"));

    auto* qualificationPage = new QWidget(tabs_);
    qualificationPage->setObjectName(QStringLiteral("qualificationPage"));
    auto* qualificationLayout = new QVBoxLayout(qualificationPage);
    qualification_dashboard_ = new QLabel(qualificationPage);
    qualification_dashboard_->setObjectName(QStringLiteral("qualificationDashboard"));
    failure_tree_ = new QTreeWidget(qualificationPage);
    failure_tree_->setObjectName(QStringLiteral("failureTree"));
    failure_tree_->setHeaderLabels({tr("Assertion / point"), tr("Observed"), tr("Threshold")});
    qualificationLayout->addWidget(qualification_dashboard_);
    qualificationLayout->addWidget(failure_tree_, 1);
    tabs_->addTab(qualificationPage, tr("Qualification"));

    auto* calibrationPage = new QWidget(tabs_);
    calibrationPage->setObjectName(QStringLiteral("calibrationPage"));
    auto* calibrationLayout = new QVBoxLayout(calibrationPage);
    calibration_table_ = new QTableWidget(calibrationPage);
    calibration_table_->setObjectName(QStringLiteral("calibrationTable"));
    calibration_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    calibrationLayout->addWidget(calibration_table_);
    tabs_->addTab(calibrationPage, tr("Calibration"));

    auto* legacyPage = new QWidget(tabs_);
    legacyPage->setObjectName(QStringLiteral("legacyPage"));
    auto* legacyLayout = new QVBoxLayout(legacyPage);
    legacyLayout->addWidget(metrics_);
    legacyLayout->addWidget(show_reference_);
    legacyLayout->addWidget(overlap_notice_);
    legacyLayout->addWidget(chartView, 1);
    legacyLayout->addWidget(correctionChartView, 1);
    tabs_->addTab(legacyPage, tr("Legacy chart"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(status_);
    layout->addWidget(sample_count_);
    layout->addWidget(result_summary_);
    layout->addWidget(tabs_, 1);

    connect(show_reference_, &QCheckBox::toggled, this, [this](bool visible) {
        reference_series_->setVisible(visible);
        updatePowerAxis();
        updateOverlapNotice();
    });
    connect(qualification_filter_, &QComboBox::currentIndexChanged, this,
            [this] { rebuildPointTable(); });
    connect(plot_axis_, &QComboBox::currentIndexChanged, this,
            [this] { rebuildPlot(); });
    connect(plot_scalar_, &QComboBox::currentIndexChanged, this,
            [this] { rebuildPlot(); });
    connect(heatmap_x_, &QComboBox::currentIndexChanged, this,
            [this] { rebuildHeatmap(); });
    connect(heatmap_y_, &QComboBox::currentIndexChanged, this,
            [this] { rebuildHeatmap(); });
    connect(heatmap_scalar_, &QComboBox::currentIndexChanged, this,
            [this] { rebuildHeatmap(); });
    connect(heatmap_, &QTableWidget::itemClicked, this,
            [this](QTableWidgetItem* item) {
                if (!item || !item->data(Qt::UserRole).isValid()) return;
                const int pointIndex = item->data(Qt::UserRole).toInt();
                qualification_filter_->setCurrentIndex(0);
                for (QComboBox* filter : coordinate_filters_) filter->setCurrentIndex(0);
                rebuildPointTable();
                for (int row = 0; row < point_table_->rowCount(); ++row) {
                    if (point_table_->item(row, 0)->data(Qt::UserRole).toInt() == pointIndex) {
                        point_table_->selectRow(row);
                        point_table_->scrollToItem(point_table_->item(row, 0));
                        break;
                    }
                }
                tabs_->setCurrentWidget(point_table_->parentWidget());
            });
    connect(failure_tree_, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem* item) {
                const int line = item->data(0, Qt::UserRole).toInt();
                const int column = item->data(0, Qt::UserRole + 1).toInt();
                if (line > 0 && column > 0) emit sourceNavigationRequested(line, column);
            });
}

void ResultsView::setResults(const ResultsModel& model, const QString& cliReport) {
    const bool hadStructuredResults = has_results_ && model_.isStructured();
    const ResultShape previousShape = resultShape(model_);
    const SavedSelection saved = hadStructuredResults ? captureSelection() : SavedSelection{};
    model_ = model;
    if (model_.isStructured()) {
        setStructuredResults(
            saved, hadStructuredResults && previousShape.isCompatibleWith(resultShape(model_)));
        return;
    }
    const int legacyIndex = tabs_->indexOf(metrics_->parentWidget());
    tabs_->setTabEnabled(legacyIndex, true);
    tabs_->setCurrentIndex(legacyIndex);
    sample_count_->setVisible(true);
    result_summary_->clear();
    result_summary_->setVisible(false);
    reference_series_->clear();
    measured_series_->clear();
    corrected_series_->clear();
    correction_series_->clear();

    double minimumFrequency = std::numeric_limits<double>::infinity();
    double maximumFrequency = -std::numeric_limits<double>::infinity();
    double minimumCorrection = std::numeric_limits<double>::infinity();
    double maximumCorrection = -std::numeric_limits<double>::infinity();
    curves_overlap_ = true;
    for (const ResultSample& sample : model_.samples()) {
        const double frequencyGhz = sample.frequency_hz / 1.0e9;
        reference_series_->append(frequencyGhz, sample.reference_power_dbm);
        measured_series_->append(frequencyGhz, sample.measured_power_dbm);
        corrected_series_->append(frequencyGhz, sample.corrected_power_dbm);
        correction_series_->append(frequencyGhz, sample.correction_db);
        minimumFrequency = std::min(minimumFrequency, frequencyGhz);
        maximumFrequency = std::max(maximumFrequency, frequencyGhz);
        minimumCorrection = std::min(minimumCorrection, sample.correction_db);
        maximumCorrection = std::max(maximumCorrection, sample.correction_db);
        curves_overlap_ = curves_overlap_ &&
            sample.reference_power_dbm == sample.corrected_power_dbm;
    }

    const AxisRange frequencyRange =
        paddedRange(minimumFrequency, maximumFrequency, 0.002);
    frequency_axis_->setRange(frequencyRange.minimum, frequencyRange.maximum);
    correction_frequency_axis_->setRange(frequencyRange.minimum, frequencyRange.maximum);
    const AxisRange correctionRange =
        paddedRange(minimumCorrection, maximumCorrection, 1.0);
    correction_axis_->setRange(correctionRange.minimum, correctionRange.maximum);
    const int ticks = std::clamp(static_cast<int>(model_.samples().size()), 2, 6);
    frequency_axis_->setTickCount(ticks);
    correction_frequency_axis_->setTickCount(ticks);
    power_axis_->setTickCount(6);
    correction_axis_->setTickCount(6);
    reference_series_->setVisible(show_reference_->isChecked());
    updatePowerAxis();
    updateOverlapNotice();

    has_results_ = true;
    status_->setText(tr("Experiment completed."));
    sample_count_->setText(tr("Samples: %1").arg(model_.samples().size()));
    metrics_->setText(metricText(cliReport));
}

ResultsView::ResultShape ResultsView::resultShape(const ResultsModel& model) const {
    ResultShape shape;
    if (!model.isStructured() || model.points().isEmpty()) return shape;
    const ResultPoint& first = model.points().front();
    for (const ResultCoordinate& coordinate : first.coordinates) {
        shape.coordinates.append(coordinate.name + QChar(u'\x1f') + coordinate.value.type);
    }
    for (const ResultObservation& measurement : first.measurements) {
        if (!measurement.value.is_boolean) {
            shape.measurements.append(measurement.name + QChar(u'\x1f') + measurement.value.type);
        }
    }
    for (const ResultObservation& value : first.derived_values) {
        if (!value.value.is_boolean) {
            shape.derived_values.append(value.name + QChar(u'\x1f') + value.value.type);
        }
    }
    shape.measurements.sort();
    shape.derived_values.sort();
    shape.has_assertions = !first.assertions.isEmpty();
    shape.has_calibrations = !model.calibrations().isEmpty();
    return shape;
}

ResultsView::SavedSelection ResultsView::captureSelection() const {
    SavedSelection saved;
    if (tabs_->currentWidget() != nullptr) saved.page = tabs_->currentWidget()->objectName();
    saved.plot_axis = plot_axis_->currentData().toString();
    saved.plot_scalar = plot_scalar_->currentData().toString();
    saved.heatmap_x = heatmap_x_->currentData().toString();
    saved.heatmap_y = heatmap_y_->currentData().toString();
    saved.heatmap_scalar = heatmap_scalar_->currentData().toString();
    if (!model_.points().isEmpty()) {
        const auto& coordinates = model_.points().front().coordinates;
        for (qsizetype index = 0;
             index < coordinates.size() && index < plot_fixed_filters_.size(); ++index) {
            const QComboBox* filter = plot_fixed_filters_.at(index);
            if (filter->currentIndex() > 0) {
                saved.plot_fixed_values.insert(coordinates.at(index).name,
                                               filter->currentData());
            }
        }
    }
    return saved;
}

int ResultsView::coordinateIndex(const QString& name) const {
    if (!model_.isStructured() || model_.points().isEmpty()) return -1;
    const auto& coordinates = model_.points().front().coordinates;
    for (qsizetype index = 0; index < coordinates.size(); ++index) {
        if (coordinates.at(index).name == name) return static_cast<int>(index);
    }
    return -1;
}

int ResultsView::tabIndex(const QString& page) const {
    for (int index = 0; index < tabs_->count(); ++index) {
        if (tabs_->widget(index)->objectName() == page) return index;
    }
    return -1;
}

const ResultValue* ResultsView::scalarValue(const ResultPoint& point,
                                             const QString& name) const {
    for (const ResultObservation& value : point.measurements) {
        if (value.name == name) return &value.value;
    }
    for (const ResultObservation& value : point.derived_values) {
        if (value.name == name) return &value.value;
    }
    return nullptr;
}

void ResultsView::setStructuredResults(const SavedSelection& saved, bool compatible) {
    const ResultPoint& first = model_.points().front();
    const QSignalBlocker plotAxisBlocker(plot_axis_);
    const QSignalBlocker plotScalarBlocker(plot_scalar_);
    const QSignalBlocker heatmapXBlocker(heatmap_x_);
    const QSignalBlocker heatmapYBlocker(heatmap_y_);
    const QSignalBlocker heatmapScalarBlocker(heatmap_scalar_);
    while (coordinate_filter_layout_->rowCount() > 1) {
        coordinate_filter_layout_->removeRow(coordinate_filter_layout_->rowCount() - 1);
    }
    while (plot_fixed_layout_->rowCount() > 2) {
        plot_fixed_layout_->removeRow(plot_fixed_layout_->rowCount() - 1);
    }
    coordinate_filters_.clear();
    plot_fixed_filters_.clear();
    plot_axis_->clear();
    heatmap_x_->clear();
    heatmap_y_->clear();
    for (qsizetype dimension = 0; dimension < first.coordinates.size(); ++dimension) {
        const QString name = first.coordinates.at(dimension).name;
        plot_axis_->addItem(name, name);
        heatmap_x_->addItem(name, name);
        heatmap_y_->addItem(name, name);

        auto populate = [this, dimension](QComboBox* combo) {
            combo->addItem(tr("All values"), QVariant{});
            std::set<double> seen;
            for (const ResultPoint& point : model_.points()) {
                if (dimension >= point.coordinates.size()) continue;
                const auto& coordinate = point.coordinates.at(dimension);
                if (seen.insert(coordinate.value.value).second) {
                    combo->addItem(coordinate.value.displayText(), coordinate.value.value);
                }
            }
        };
        auto* tableFilter = new QComboBox(this);
        tableFilter->setObjectName(QStringLiteral("coordinateFilter_%1").arg(name));
        populate(tableFilter);
        coordinate_filter_layout_->addRow(name, tableFilter);
        coordinate_filters_.append(tableFilter);
        connect(tableFilter, &QComboBox::currentIndexChanged, this,
                [this] { rebuildPointTable(); });

        auto* plotFilter = new QComboBox(this);
        plotFilter->setObjectName(QStringLiteral("plotFixed_%1").arg(name));
        populate(plotFilter);
        if (dimension != 0 && plotFilter->count() > 1) plotFilter->setCurrentIndex(1);
        plot_fixed_layout_->addRow(tr("Fix %1").arg(name), plotFilter);
        plot_fixed_filters_.append(plotFilter);
    }

    plot_scalar_->clear();
    heatmap_scalar_->clear();
    auto addScalar = [this](const ResultObservation& observation) {
        if (observation.value.is_boolean) return;
        const QString label = QStringLiteral("%1 (%2)")
                                  .arg(observation.name, observation.value.display_unit);
        plot_scalar_->addItem(label, observation.name);
        heatmap_scalar_->addItem(label, observation.name);
    };
    for (const ResultObservation& observation : first.measurements) addScalar(observation);
    for (const ResultObservation& observation : first.derived_values) addScalar(observation);

    auto selectData = [](QComboBox* combo, const QVariant& value) {
        const int index = combo->findData(value);
        if (index >= 0) combo->setCurrentIndex(index);
        return index >= 0;
    };
    auto preferredScalar = [&first] {
        for (const ResultObservation& value : first.derived_values) {
            if (!value.value.is_boolean && value.name == QStringLiteral("power_error")) {
                return value.name;
            }
        }
        for (const ResultObservation& value : first.derived_values) {
            if (!value.value.is_boolean) return value.name;
        }
        for (const ResultObservation& value : first.measurements) {
            if (!value.value.is_boolean) return value.name;
        }
        return QString{};
    };
    const QString defaultScalar = preferredScalar();

    const QString defaultPlotAxis = coordinateIndex(QStringLiteral("frequency")) >= 0
        ? QStringLiteral("frequency") : plot_axis_->itemData(0).toString();
    if (!compatible || !selectData(plot_axis_, saved.plot_axis)) {
        selectData(plot_axis_, defaultPlotAxis);
    }
    if (!compatible || !selectData(plot_scalar_, saved.plot_scalar)) {
        selectData(plot_scalar_, defaultScalar);
    }

    const QString defaultHeatmapX = coordinateIndex(QStringLiteral("frequency")) >= 0
        ? QStringLiteral("frequency") : heatmap_x_->itemData(0).toString();
    QString defaultHeatmapY = coordinateIndex(QStringLiteral("temperature")) >= 0
        ? QStringLiteral("temperature") : heatmap_y_->itemData(1).toString();
    if (defaultHeatmapY == defaultHeatmapX && heatmap_y_->count() > 1) {
        defaultHeatmapY = heatmap_y_->itemData(defaultHeatmapX == heatmap_y_->itemData(0).toString()
                                                   ? 1 : 0).toString();
    }
    const bool restoredHeatmapX = compatible && selectData(heatmap_x_, saved.heatmap_x);
    const bool restoredHeatmapY = compatible && selectData(heatmap_y_, saved.heatmap_y);
    if (!restoredHeatmapX) selectData(heatmap_x_, defaultHeatmapX);
    if (!restoredHeatmapY) selectData(heatmap_y_, defaultHeatmapY);
    if (heatmap_x_->currentData() == heatmap_y_->currentData() && heatmap_y_->count() > 1) {
        selectData(heatmap_y_, defaultHeatmapY);
        if (heatmap_x_->currentData() == heatmap_y_->currentData()) {
            heatmap_y_->setCurrentIndex(heatmap_x_->currentIndex() == 0 ? 1 : 0);
        }
    }
    if (!compatible || !selectData(heatmap_scalar_, saved.heatmap_scalar)) {
        selectData(heatmap_scalar_, defaultScalar);
    }

    const int plotAxisIndex = coordinateIndex(plot_axis_->currentData().toString());
    for (qsizetype dimension = 0; dimension < plot_fixed_filters_.size(); ++dimension) {
        QComboBox* filter = plot_fixed_filters_.at(dimension);
        if (dimension == plotAxisIndex) {
            filter->setCurrentIndex(0);
            filter->setEnabled(false);
        } else {
            filter->setEnabled(true);
            const QString name = first.coordinates.at(dimension).name;
            const int restored = compatible && saved.plot_fixed_values.contains(name)
                ? filter->findData(saved.plot_fixed_values.value(name)) : -1;
            filter->setCurrentIndex(restored >= 0 ? restored : std::min(1, filter->count() - 1));
        }
        connect(filter, &QComboBox::currentIndexChanged, this,
                [this] { rebuildPlot(); });
    }

    const bool plotUsable = plot_axis_->count() > 0 && plot_scalar_->count() > 0;
    const bool heatmapUsable = heatmap_x_->count() > 1 && heatmap_scalar_->count() > 0;
    const int pointsIndex = tabIndex(QStringLiteral("pointsPage"));
    const int plotIndex = tabIndex(QStringLiteral("plotPage"));
    const int heatmapIndex = tabIndex(QStringLiteral("heatmapPage"));
    const int qualificationIndex = tabIndex(QStringLiteral("qualificationPage"));
    const int calibrationIndex = tabIndex(QStringLiteral("calibrationPage"));
    const int legacyIndex = tabIndex(QStringLiteral("legacyPage"));
    tabs_->setTabEnabled(pointsIndex, true);
    tabs_->setTabEnabled(plotIndex, plotUsable);
    tabs_->setTabEnabled(heatmapIndex, heatmapUsable);
    tabs_->setTabEnabled(qualificationIndex, !first.assertions.isEmpty());
    tabs_->setTabEnabled(calibrationIndex, !model_.calibrations().isEmpty());
    tabs_->setTabEnabled(legacyIndex, false);

    const int savedPageIndex = compatible ? tabIndex(saved.page) : -1;
    if (savedPageIndex >= 0 && tabs_->isTabEnabled(savedPageIndex)) {
        tabs_->setCurrentIndex(savedPageIndex);
    } else if (heatmapUsable) {
        tabs_->setCurrentIndex(heatmapIndex);
    } else if (plotUsable) {
        tabs_->setCurrentIndex(plotIndex);
    } else {
        tabs_->setCurrentIndex(pointsIndex);
    }

    has_results_ = true;
    status_->setText(model_.qualification().status == QStringLiteral("fail")
                         ? tr("Experiment completed; qualification failed.")
                         : tr("Experiment completed."));
    sample_count_->setVisible(false);
    rebuildStructuredSummary();
    rebuildPointTable();
    rebuildPlot();
    rebuildHeatmap();
    rebuildQualification();
    rebuildCalibration();
}

void ResultsView::rebuildStructuredSummary() {
    if (!model_.isStructured() || model_.points().isEmpty()) return;

    const ResultPoint& first = model_.points().front();
    QStringList dimensions;
    for (const ResultCoordinate& coordinate : first.coordinates) {
        dimensions.append(coordinate.name);
    }

    QStringList measurements;
    for (const ResultObservation& measurement : first.measurements) {
        measurements.append(tr("%1 (%2)").arg(measurement.name, measurement.operation));
    }

    QString qualification;
    const QualificationSummary& summary = model_.qualification();
    if (summary.assertions.isEmpty()) {
        qualification = tr("not evaluated (no assertions)");
    } else if (summary.status == QStringLiteral("pass")) {
        qualification = tr("passed");
    } else if (summary.status == QStringLiteral("fail")) {
        qualification = tr("failed");
    } else {
        qualification = tr("not evaluated");
    }

    QStringList calibrations;
    for (const CalibrationSurface& calibration : model_.calibrations()) {
        calibrations.append(calibration.name);
    }

    result_summary_->setText(
        tr("%1 — %2 points\nSweep dimensions: %3\nMeasurements: %4\n"
           "Qualification: %5\nCalibration surfaces: %6")
            .arg(model_.characterization())
            .arg(model_.points().size())
            .arg(dimensions.isEmpty() ? tr("none") : dimensions.join(QStringLiteral(", ")))
            .arg(measurements.isEmpty() ? tr("none") : measurements.join(QStringLiteral(", ")))
            .arg(qualification)
            .arg(calibrations.isEmpty() ? tr("none") : calibrations.join(QStringLiteral(", "))));
    result_summary_->setVisible(true);
}

void ResultsView::rebuildPointTable() {
    if (!model_.isStructured() || model_.points().isEmpty()) return;
    const ResultPoint& first = model_.points().front();
    QStringList headers;
    for (const auto& coordinate : first.coordinates) {
        headers.append(QStringLiteral("%1 (%2)").arg(coordinate.name,
                                                      coordinate.value.display_unit));
    }
    for (const auto& observation : first.measurements) {
        headers.append(QStringLiteral("%1 (%2)").arg(observation.name,
                                                      observation.value.display_unit));
    }
    for (const auto& value : first.derived_values) {
        headers.append(QStringLiteral("%1 (%2)").arg(value.name, value.value.display_unit));
    }
    headers.append(tr("Qualification"));
    point_table_->clear();
    point_table_->setColumnCount(headers.size());
    point_table_->setHorizontalHeaderLabels(headers);
    point_table_->setRowCount(0);
    const QStringList states{QString{}, QStringLiteral("pass"), QStringLiteral("fail"),
                             QStringLiteral("not_evaluated")};
    const QString requiredState = states.value(qualification_filter_->currentIndex());
    for (qsizetype pointIndex = 0; pointIndex < model_.points().size(); ++pointIndex) {
        const ResultPoint& point = model_.points().at(pointIndex);
        if (!requiredState.isEmpty() && point.qualification != requiredState) continue;
        bool matches = true;
        for (qsizetype dimension = 0; dimension < coordinate_filters_.size(); ++dimension) {
            const QComboBox* filter = coordinate_filters_.at(dimension);
            if (filter->currentIndex() > 0 &&
                point.coordinates.at(dimension).value.value != filter->currentData().toDouble()) {
                matches = false;
                break;
            }
        }
        if (!matches) continue;
        const int row = point_table_->rowCount();
        point_table_->insertRow(row);
        int column = 0;
        auto addValue = [&](const ResultValue& value) {
            auto* item = new QTableWidgetItem(value.displayText());
            item->setData(Qt::UserRole, pointIndex);
            point_table_->setItem(row, column++, item);
        };
        for (const auto& coordinate : point.coordinates) addValue(coordinate.value);
        for (const auto& observation : point.measurements) addValue(observation.value);
        for (const auto& value : point.derived_values) addValue(value.value);
        auto* state = new QTableWidgetItem(point.qualification);
        state->setData(Qt::UserRole, pointIndex);
        state->setForeground(point.qualification == QStringLiteral("fail")
                                 ? QColor(QStringLiteral("#b71c1c"))
                                 : QColor(QStringLiteral("#1b5e20")));
        point_table_->setItem(row, column, state);
    }
    point_table_->resizeColumnsToContents();
}

void ResultsView::rebuildPlot() {
    selected_series_->clear();
    if (!model_.isStructured() || plot_axis_->currentIndex() < 0 ||
        plot_scalar_->currentIndex() < 0) return;
    const int axis = coordinateIndex(plot_axis_->currentData().toString());
    if (axis < 0) return;
    for (qsizetype dimension = 0; dimension < plot_fixed_filters_.size(); ++dimension) {
        QComboBox* filter = plot_fixed_filters_.at(dimension);
        const QSignalBlocker blocker(filter);
        filter->setEnabled(dimension != axis);
        if (dimension == axis) {
            filter->setCurrentIndex(0);
        } else if (filter->currentIndex() == 0 && filter->count() > 1) {
            filter->setCurrentIndex(1);
        }
    }
    const QString scalar = plot_scalar_->currentData().toString();
    QString xUnit;
    QString yUnit;
    QList<QPointF> selectedPoints;
    for (const ResultPoint& point : model_.points()) {
        bool matches = true;
        for (qsizetype dimension = 0; dimension < plot_fixed_filters_.size(); ++dimension) {
            if (dimension == axis) continue;
            const QComboBox* filter = plot_fixed_filters_.at(dimension);
            if (filter->currentIndex() > 0 &&
                point.coordinates.at(dimension).value.value != filter->currentData().toDouble()) {
                matches = false;
                break;
            }
        }
        const ResultValue* y = scalarValue(point, scalar);
        if (!matches || y == nullptr || y->is_boolean) continue;
        const ResultValue& x = point.coordinates.at(axis).value;
        selectedPoints.append(QPointF{x.display_value, y->display_value});
        xUnit = x.display_unit;
        yUnit = y->display_unit;
    }
    std::sort(selectedPoints.begin(), selectedPoints.end(),
              [](const QPointF& left, const QPointF& right) { return left.x() < right.x(); });
    selected_series_->replace(selectedPoints);
    selected_x_axis_->setTitleText(QStringLiteral("%1 (%2)").arg(plot_axis_->currentText(), xUnit));
    selected_y_axis_->setTitleText(QStringLiteral("%1 (%2)").arg(scalar, yUnit));
    if (selected_series_->count() == 0) return;
    double minX = selected_series_->at(0).x();
    double maxX = minX;
    double minY = selected_series_->at(0).y();
    double maxY = minY;
    for (const QPointF& point : selected_series_->points()) {
        minX = std::min(minX, point.x()); maxX = std::max(maxX, point.x());
        minY = std::min(minY, point.y()); maxY = std::max(maxY, point.y());
    }
    const AxisRange xRange = paddedRange(minX, maxX, 1.0);
    const AxisRange yRange = paddedRange(minY, maxY, 1.0);
    selected_x_axis_->setRange(xRange.minimum, xRange.maximum);
    selected_y_axis_->setRange(yRange.minimum, yRange.maximum);
}

void ResultsView::rebuildHeatmap() {
    heatmap_->clear();
    heatmap_->setRowCount(0);
    heatmap_->setColumnCount(0);
    if (!model_.isStructured() || heatmap_x_->count() < 2 ||
        heatmap_x_->currentIndex() == heatmap_y_->currentIndex() ||
        heatmap_scalar_->currentIndex() < 0) return;
    const int xDimension = coordinateIndex(heatmap_x_->currentData().toString());
    const int yDimension = coordinateIndex(heatmap_y_->currentData().toString());
    if (xDimension < 0 || yDimension < 0 || xDimension == yDimension) return;
    const QString scalar = heatmap_scalar_->currentData().toString();
    const ResultPoint& first = model_.points().front();
    auto inSelectedSlice = [&](const ResultPoint& point) {
        for (qsizetype dimension = 0; dimension < point.coordinates.size(); ++dimension) {
            if (dimension == xDimension || dimension == yDimension) continue;
            if (point.coordinates.at(dimension).value.value !=
                first.coordinates.at(dimension).value.value) return false;
        }
        return true;
    };
    QVector<double> xs;
    QVector<double> ys;
    QStringList xLabels;
    QStringList yLabels;
    for (const ResultPoint& point : model_.points()) {
        const auto& x = point.coordinates.at(xDimension).value;
        const auto& y = point.coordinates.at(yDimension).value;
        if (!xs.contains(x.value)) { xs.append(x.value); xLabels.append(x.displayText()); }
        if (!ys.contains(y.value)) { ys.append(y.value); yLabels.append(y.displayText()); }
    }
    heatmap_->setColumnCount(xs.size());
    heatmap_->setRowCount(ys.size());
    heatmap_->setHorizontalHeaderLabels(xLabels);
    heatmap_->setVerticalHeaderLabels(yLabels);
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (const ResultPoint& point : model_.points()) {
        if (!inSelectedSlice(point)) continue;
        const ResultValue* value = scalarValue(point, scalar);
        if (value != nullptr) { minimum = std::min(minimum, value->display_value);
                               maximum = std::max(maximum, value->display_value); }
    }
    for (int row = 0; row < ys.size(); ++row) {
        for (int column = 0; column < xs.size(); ++column) {
            auto* item = new QTableWidgetItem(QStringLiteral("—"));
            item->setTextAlignment(Qt::AlignCenter);
            heatmap_->setItem(row, column, item);
        }
    }
    for (qsizetype index = 0; index < model_.points().size(); ++index) {
        const ResultPoint& point = model_.points().at(index);
        if (!inSelectedSlice(point)) continue;
        const int column = static_cast<int>(xs.indexOf(point.coordinates.at(xDimension).value.value));
        const int row = static_cast<int>(ys.indexOf(point.coordinates.at(yDimension).value.value));
        const ResultValue* value = scalarValue(point, scalar);
        if (row < 0 || column < 0 || value == nullptr) continue;
        auto* item = heatmap_->item(row, column);
        item->setText(value->displayText());
        item->setData(Qt::UserRole, index);
        const double fraction = maximum > minimum
            ? (value->display_value - minimum) / (maximum - minimum) : 0.5;
        item->setBackground(QColor::fromHsvF(
            static_cast<float>(0.66 * (1.0 - fraction)), 0.55F, 0.95F));
    }
    heatmap_->resizeColumnsToContents();
}

void ResultsView::rebuildQualification() {
    const QualificationSummary& summary = model_.qualification();
    const double percentage = summary.total_points == 0
        ? 0.0 : 100.0 * summary.passed_points / summary.total_points;
    qualification_dashboard_->setText(
        tr("Overall: %1 | Points: %2 total, %3 passed, %4 failed | Pass: %5%")
            .arg(summary.status).arg(summary.total_points).arg(summary.passed_points)
            .arg(summary.failed_points).arg(percentage, 0, 'f', 1));
    failure_tree_->clear();
    for (const AssertionSummary& assertion : summary.assertions) {
        auto* group = new QTreeWidgetItem(failure_tree_,
            {tr("%1 — %2/%3 failed").arg(assertion.id).arg(assertion.failed).arg(assertion.total)});
        for (const ResultPoint& point : model_.points()) {
            for (const ResultAssertion& outcome : point.assertions) {
                if (outcome.id != assertion.id || outcome.passed) continue;
                QStringList coordinates;
                for (const auto& coordinate : point.coordinates) {
                    coordinates.append(QStringLiteral("%1=%2").arg(coordinate.name,
                                                                    coordinate.value.displayText()));
                }
                auto* failure = new QTreeWidgetItem(group,
                    {coordinates.join(QStringLiteral(", ")), outcome.left.displayText(),
                     outcome.right.displayText()});
                failure->setData(0, Qt::UserRole, outcome.begin.line);
                failure->setData(0, Qt::UserRole + 1, outcome.begin.column);
                failure->setToolTip(0, tr("Activate to navigate to the assertion source."));
            }
        }
        group->setExpanded(assertion.failed > 0);
    }
    failure_tree_->resizeColumnToContents(0);
}

void ResultsView::rebuildCalibration() {
    calibration_table_->clear();
    if (model_.calibrations().isEmpty()) {
        calibration_table_->setRowCount(0);
        calibration_table_->setColumnCount(0);
        return;
    }
    const CalibrationSurface& surface = model_.calibrations().front();
    QStringList headers;
    for (const auto& dimension : surface.dimensions) headers.append(dimension.name);
    headers.append(QStringLiteral("%1 correction (%2)").arg(surface.name, surface.canonical_unit));
    calibration_table_->setColumnCount(headers.size());
    calibration_table_->setHorizontalHeaderLabels(headers);
    calibration_table_->setRowCount(surface.entries.size());
    for (qsizetype row = 0; row < surface.entries.size(); ++row) {
        const CalibrationEntry& entry = surface.entries.at(row);
        int column = 0;
        for (const auto& coordinate : entry.coordinates) {
            calibration_table_->setItem(row, column++,
                                         new QTableWidgetItem(coordinate.value.displayText()));
        }
        calibration_table_->setItem(row, column,
                                     new QTableWidgetItem(entry.correction.displayText()));
    }
    calibration_table_->resizeColumnsToContents();
}

void ResultsView::updatePowerAxis() {
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (const QLineSeries* series : {reference_series_, measured_series_, corrected_series_}) {
        if (!series->isVisible()) continue;
        for (const QPointF& point : series->points()) {
            minimum = std::min(minimum, point.y());
            maximum = std::max(maximum, point.y());
        }
    }
    if (!std::isfinite(minimum) || !std::isfinite(maximum)) return;
    const AxisRange range = paddedRange(minimum, maximum, 1.0);
    power_axis_->setRange(range.minimum, range.maximum);
}

void ResultsView::updateOverlapNotice() {
    const bool visible = curves_overlap_ && reference_series_->isVisible();
    overlap_notice_->setVisible(visible);
    overlap_notice_->setText(
        visible ? tr("Reference and corrected power overlap exactly. Blue dashes overlay "
                     "the solid orange corrected curve so both remain identifiable.")
                : QString{});
}

void ResultsView::setStatusMessage(const QString& message) {
    status_->setText(message);
}

} // namespace horusrf::studio
