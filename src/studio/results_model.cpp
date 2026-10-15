// SPDX-License-Identifier: GPL-3.0-only
#include "results_model.hpp"

#include <QFile>
#include <QList>

#include <array>
#include <charconv>
#include <cmath>

namespace horusrf::studio {
namespace {

constexpr auto expected_header =
    "frequency_hz,reference_power_dbm,measured_power_dbm,error_db,correction_db,"
    "corrected_power_dbm,residual_error_db";

void setError(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
}

bool parseFiniteNumber(const QByteArray& field, double& value) {
    if (field.isEmpty()) return false;
    const char* const begin = field.constData();
    const char* const end = begin + field.size();
    const auto parsed = std::from_chars(begin, end, value, std::chars_format::general);
    return parsed.ec == std::errc{} && parsed.ptr == end && std::isfinite(value);
}

} // namespace

std::optional<ResultsModel> ResultsModel::fromCsvFile(const QString& path,
                                                       QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QObject::tr("Could not read %1: %2")
                            .arg(path, file.errorString()));
        return std::nullopt;
    }
    return fromCsv(file.readAll(), error);
}

std::optional<ResultsModel> ResultsModel::fromCsv(const QByteArray& csv,
                                                   QString* error) {
    QList<QByteArray> lines = csv.split('\n');
    if (!lines.isEmpty() && lines.back().isEmpty()) lines.removeLast();
    for (QByteArray& line : lines) {
        if (line.endsWith('\r')) line.chop(1);
    }

    if (lines.isEmpty() || lines.front() != expected_header) {
        setError(error, QObject::tr("The result CSV header is missing or invalid."));
        return std::nullopt;
    }
    if (lines.size() == 1) {
        setError(error, QObject::tr("The result CSV contains no samples."));
        return std::nullopt;
    }

    ResultsModel model;
    model.samples_.reserve(lines.size() - 1);
    for (qsizetype row = 1; row < lines.size(); ++row) {
        const QList<QByteArray> fields = lines.at(row).split(',');
        if (fields.size() != 7) {
            setError(error, QObject::tr("Result CSV row %1 must contain exactly seven fields.")
                                .arg(row + 1));
            return std::nullopt;
        }

        std::array<double, 7> values{};
        for (qsizetype column = 0; column < fields.size(); ++column) {
            if (!parseFiniteNumber(fields.at(column), values.at(column))) {
                setError(error,
                         QObject::tr("Result CSV row %1, field %2 is not a finite number.")
                             .arg(row + 1)
                             .arg(column + 1));
                return std::nullopt;
            }
        }
        model.samples_.append(ResultSample{values[0], values[1], values[2], values[3],
                                           values[4], values[5], values[6]});
    }
    return model;
}

} // namespace horusrf::studio
