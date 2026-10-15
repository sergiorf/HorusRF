// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "results_model.hpp"

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineSeries;
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
        return model_.samples().size();
    }

private:
    ResultsModel model_;
    QLineSeries* reference_series_ = nullptr;
    QLineSeries* measured_series_ = nullptr;
    QLineSeries* corrected_series_ = nullptr;
    QValueAxis* frequency_axis_ = nullptr;
    QValueAxis* power_axis_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* sample_count_ = nullptr;
    QLabel* metrics_ = nullptr;
    QCheckBox* show_reference_ = nullptr;
    bool has_results_ = false;
};

} // namespace horusrf::studio
