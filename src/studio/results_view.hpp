// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "results_model.hpp"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineSeries;
class QTableWidget;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QValueAxis;

namespace horusrf::studio {

class ResultsView final : public QWidget {
    Q_OBJECT

public:
    explicit ResultsView(QWidget* parent = nullptr);

    void setResults(const ResultsModel& model, const QString& cliReport);
    void setStatusMessage(const QString& message);

    [[nodiscard]] bool hasResults() const noexcept { return has_results_; }
    [[nodiscard]] qsizetype sampleCount() const noexcept {
        return model_.isStructured() ? model_.points().size() : model_.samples().size();
    }

signals:
    void sourceNavigationRequested(int line, int column);

private:
    void setStructuredResults();
    void rebuildPointTable();
    void rebuildPlot();
    void rebuildHeatmap();
    void rebuildQualification();
    void rebuildCalibration();
    [[nodiscard]] const ResultValue* scalarValue(const ResultPoint& point,
                                                  const QString& name) const;
    void updatePowerAxis();
    void updateOverlapNotice();

    ResultsModel model_;
    QLineSeries* reference_series_ = nullptr;
    QLineSeries* measured_series_ = nullptr;
    QLineSeries* corrected_series_ = nullptr;
    QLineSeries* correction_series_ = nullptr;
    QValueAxis* frequency_axis_ = nullptr;
    QValueAxis* power_axis_ = nullptr;
    QValueAxis* correction_frequency_axis_ = nullptr;
    QValueAxis* correction_axis_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* sample_count_ = nullptr;
    QLabel* metrics_ = nullptr;
    QLabel* overlap_notice_ = nullptr;
    QCheckBox* show_reference_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QTableWidget* point_table_ = nullptr;
    QComboBox* qualification_filter_ = nullptr;
    QFormLayout* coordinate_filter_layout_ = nullptr;
    QVector<QComboBox*> coordinate_filters_;
    QComboBox* plot_axis_ = nullptr;
    QComboBox* plot_scalar_ = nullptr;
    QFormLayout* plot_fixed_layout_ = nullptr;
    QVector<QComboBox*> plot_fixed_filters_;
    QLineSeries* selected_series_ = nullptr;
    QValueAxis* selected_x_axis_ = nullptr;
    QValueAxis* selected_y_axis_ = nullptr;
    QComboBox* heatmap_x_ = nullptr;
    QComboBox* heatmap_y_ = nullptr;
    QComboBox* heatmap_scalar_ = nullptr;
    QTableWidget* heatmap_ = nullptr;
    QLabel* qualification_dashboard_ = nullptr;
    QTreeWidget* failure_tree_ = nullptr;
    QTableWidget* calibration_table_ = nullptr;
    bool has_results_ = false;
    bool curves_overlap_ = false;
};

} // namespace horusrf::studio
