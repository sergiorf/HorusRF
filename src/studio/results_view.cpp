// SPDX-License-Identifier: GPL-3.0-only
#include "results_view.hpp"

#include <QChart>
#include <QChartView>
#include <QCheckBox>
#include <QLabel>
#include <QLineSeries>
#include <QStringList>
#include <QValueAxis>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace horusrf::studio {
namespace {

void expandDegenerateRange(double& minimum, double& maximum, double minimumSpan) {
    if (maximum > minimum) return;
    const double padding = std::max(minimumSpan, std::abs(minimum) * 0.01);
    minimum -= padding;
    maximum += padding;
}

QString metricText(const QString& report) {
    QStringList metrics;
    for (const QString& line : report.split('\n', Qt::SkipEmptyParts)) {
        if (line.startsWith(QStringLiteral("uncorrected_rms_error_db:")) ||
            line.startsWith(QStringLiteral("corrected_rms_error_db:")) ||
            line.startsWith(QStringLiteral("maximum_absolute_error_db:")) ||
            line.startsWith(QStringLiteral("corrected_maximum_absolute_error_db:")) ||
            line.startsWith(QStringLiteral("rms_improvement_ratio:"))) {
            metrics.append(line);
        }
    }
    return metrics.join('\n');
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

    auto* chart = new QChart;
    chart->setTitle(tr("Power over frequency"));
    chart->addSeries(reference_series_);
    chart->addSeries(measured_series_);
    chart->addSeries(corrected_series_);
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

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(status_);
    layout->addWidget(sample_count_);
    layout->addWidget(metrics_);
    layout->addWidget(show_reference_);
    layout->addWidget(chartView, 1);

    connect(show_reference_, &QCheckBox::toggled, reference_series_,
            &QLineSeries::setVisible);
}

void ResultsView::setResults(const ResultsModel& model, const QString& cliReport) {
    model_ = model;
    reference_series_->clear();
    measured_series_->clear();
    corrected_series_->clear();

    double minimumFrequency = std::numeric_limits<double>::infinity();
    double maximumFrequency = -std::numeric_limits<double>::infinity();
    double minimumPower = std::numeric_limits<double>::infinity();
    double maximumPower = -std::numeric_limits<double>::infinity();
    for (const ResultSample& sample : model_.samples()) {
        const double frequencyGhz = sample.frequency_hz / 1.0e9;
        reference_series_->append(frequencyGhz, sample.reference_power_dbm);
        measured_series_->append(frequencyGhz, sample.measured_power_dbm);
        corrected_series_->append(frequencyGhz, sample.corrected_power_dbm);
        minimumFrequency = std::min(minimumFrequency, frequencyGhz);
        maximumFrequency = std::max(maximumFrequency, frequencyGhz);
        for (double power : {sample.reference_power_dbm, sample.measured_power_dbm,
                             sample.corrected_power_dbm}) {
            minimumPower = std::min(minimumPower, power);
            maximumPower = std::max(maximumPower, power);
        }
    }

    expandDegenerateRange(minimumFrequency, maximumFrequency, 0.001);
    expandDegenerateRange(minimumPower, maximumPower, 1.0);
    frequency_axis_->setRange(minimumFrequency, maximumFrequency);
    power_axis_->setRange(minimumPower, maximumPower);
    const int ticks = std::clamp(static_cast<int>(model_.samples().size()), 2, 6);
    frequency_axis_->setTickCount(ticks);
    power_axis_->setTickCount(6);
    reference_series_->setVisible(show_reference_->isChecked());

    has_results_ = true;
    status_->setText(tr("Experiment completed."));
    sample_count_->setText(tr("Samples: %1").arg(model_.samples().size()));
    metrics_->setText(metricText(cliReport));
}

void ResultsView::setStatusMessage(const QString& message) {
    status_->setText(message);
}

} // namespace horusrf::studio
