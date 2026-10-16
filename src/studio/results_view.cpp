// SPDX-License-Identifier: GPL-3.0-only
#include "results_view.hpp"

#include <QChart>
#include <QChartView>
#include <QCheckBox>
#include <QCursor>
#include <QLabel>
#include <QLineSeries>
#include <QPen>
#include <QStringList>
#include <QToolTip>
#include <QValueAxis>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

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

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(status_);
    layout->addWidget(sample_count_);
    layout->addWidget(metrics_);
    layout->addWidget(show_reference_);
    layout->addWidget(overlap_notice_);
    layout->addWidget(chartView, 1);
    layout->addWidget(correctionChartView, 1);

    connect(show_reference_, &QCheckBox::toggled, this, [this](bool visible) {
        reference_series_->setVisible(visible);
        updatePowerAxis();
        updateOverlapNotice();
    });
}

void ResultsView::setResults(const ResultsModel& model, const QString& cliReport) {
    model_ = model;
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
