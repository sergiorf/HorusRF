// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <optional>

namespace horusrf::studio {

struct ResultValue {
    QString type;
    double value = 0.0;
    QString canonical_unit;
    double display_value = 0.0;
    QString display_unit;
    bool boolean_value = false;
    bool is_boolean = false;

    [[nodiscard]] QString displayText() const;
};

struct ResultCoordinate { QString name; ResultValue value; };
struct ResultObservation { QString name; QString operation; ResultValue value; };
struct ResultSourcePosition { int line = 0; int column = 0; int offset = 0; };

struct ResultAssertion {
    QString id;
    ResultSourcePosition begin;
    ResultSourcePosition end;
    QString comparison;
    ResultValue left;
    ResultValue right;
    bool passed = false;
};

struct ResultPoint {
    QVector<ResultCoordinate> coordinates;
    QVector<ResultObservation> measurements;
    QVector<ResultObservation> derived_values;
    QVector<ResultAssertion> assertions;
    QString qualification;
};

struct AssertionSummary { QString id; int total = 0; int passed = 0; int failed = 0; };
struct QualificationSummary {
    QString status;
    int total_points = 0;
    int passed_points = 0;
    int failed_points = 0;
    QVector<AssertionSummary> assertions;
};

struct CalibrationEntry { QVector<ResultCoordinate> coordinates; ResultValue correction; };
struct CalibrationSurface {
    QString name;
    QVector<ResultCoordinate> dimensions;
    QString correction_type;
    QString canonical_unit;
    QVector<CalibrationEntry> entries;
};

// The original strict CSV shape remains part of the Studio loading contract.
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
    [[nodiscard]] static std::optional<ResultsModel> fromJsonFile(
        const QString& path, QString* error = nullptr);
    [[nodiscard]] static std::optional<ResultsModel> fromJson(
        const QByteArray& json, QString* error = nullptr);
    [[nodiscard]] static std::optional<ResultsModel> fromCsvFile(
        const QString& path, QString* error = nullptr);
    [[nodiscard]] static std::optional<ResultsModel> fromCsv(
        const QByteArray& csv, QString* error = nullptr);

    [[nodiscard]] const QString& characterization() const noexcept { return characterization_; }
    [[nodiscard]] const QualificationSummary& qualification() const noexcept { return qualification_; }
    [[nodiscard]] const QVector<ResultPoint>& points() const noexcept { return points_; }
    [[nodiscard]] const QVector<CalibrationSurface>& calibrations() const noexcept { return calibrations_; }
    [[nodiscard]] const QVector<ResultSample>& samples() const noexcept { return samples_; }
    [[nodiscard]] bool isStructured() const noexcept { return structured_; }

private:
    QString characterization_;
    QualificationSummary qualification_;
    QVector<ResultPoint> points_;
    QVector<CalibrationSurface> calibrations_;
    QVector<ResultSample> samples_;
    bool structured_ = false;
};

} // namespace horusrf::studio
