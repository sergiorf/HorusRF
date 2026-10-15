// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <QVector>

#include <optional>

namespace horusrf::studio {

struct ResultSample {
    double frequency_hz = 0.0;
    double reference_power_dbm = 0.0;
    double measured_power_dbm = 0.0;
    double error_db = 0.0;
    double correction_db = 0.0;
    double corrected_power_dbm = 0.0;
    double residual_error_db = 0.0;
};

class ResultsModel final {
public:
    [[nodiscard]] static std::optional<ResultsModel> fromCsvFile(
        const QString& path, QString* error = nullptr);
    [[nodiscard]] static std::optional<ResultsModel> fromCsv(
        const QByteArray& csv, QString* error = nullptr);

    [[nodiscard]] const QVector<ResultSample>& samples() const noexcept {
        return samples_;
    }

private:
    QVector<ResultSample> samples_;
};

} // namespace horusrf::studio
