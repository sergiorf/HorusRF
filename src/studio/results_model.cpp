// SPDX-License-Identifier: GPL-3.0-only
#include "results_model.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QList>
#include <QObject>

#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <utility>

namespace horusrf::studio {
namespace {

constexpr auto expected_header =
    "frequency_hz,reference_power_dbm,measured_power_dbm,error_db,correction_db,"
    "corrected_power_dbm,residual_error_db";

void setError(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
}

std::optional<QByteArray> readFile(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QObject::tr("Could not read %1: %2").arg(path, file.errorString()));
        return std::nullopt;
    }
    return file.readAll();
}

bool parseFiniteNumber(const QByteArray& field, double& value) {
    if (field.isEmpty()) return false;
    const char* const begin = field.constData();
    const char* const end = begin + field.size();
    const auto parsed = std::from_chars(begin, end, value, std::chars_format::general);
    return parsed.ec == std::errc{} && parsed.ptr == end && std::isfinite(value);
}

bool finiteJsonNumber(const QJsonValue& value, double& result) {
    if (!value.isDouble()) return false;
    result = value.toDouble();
    return std::isfinite(result);
}

bool parseValue(const QJsonObject& object, ResultValue& result, QString& error,
                const QString& context) {
    result.type = object.value(QStringLiteral("type")).toString();
    static const QStringList supportedTypes{
        QStringLiteral("Frequency"), QStringLiteral("FrequencyDelta"),
        QStringLiteral("Power"), QStringLiteral("PowerDelta"),
        QStringLiteral("Ratio"), QStringLiteral("Temperature"),
        QStringLiteral("TemperatureDelta"), QStringLiteral("Boolean")};
    if (!supportedTypes.contains(result.type)) {
        error = QObject::tr("%1 has a missing or unsupported value type.").arg(context);
        return false;
    }
    const QJsonValue value = object.value(QStringLiteral("value"));
    if (result.type == QStringLiteral("Boolean")) {
        if (!value.isBool()) {
            error = QObject::tr("%1 must contain a Boolean value.").arg(context);
            return false;
        }
        result.is_boolean = true;
        result.boolean_value = value.toBool();
        return true;
    }
    if (!finiteJsonNumber(value, result.value)) {
        error = QObject::tr("%1 must contain a finite numeric value.").arg(context);
        return false;
    }
    result.canonical_unit = object.value(QStringLiteral("canonical_unit")).toString();
    result.display_unit = object.value(QStringLiteral("display_unit")).toString();
    if (result.canonical_unit.isEmpty() || result.display_unit.isEmpty() ||
        !finiteJsonNumber(object.value(QStringLiteral("display_value")), result.display_value)) {
        error = QObject::tr("%1 has incomplete unit or display metadata.").arg(context);
        return false;
    }
    return true;
}

bool parseCoordinate(const QJsonValue& json, ResultCoordinate& coordinate,
                     QString& error, const QString& context) {
    if (!json.isObject()) {
        error = QObject::tr("%1 must be an object.").arg(context);
        return false;
    }
    const QJsonObject object = json.toObject();
    coordinate.name = object.value(QStringLiteral("name")).toString();
    if (coordinate.name.isEmpty()) {
        error = QObject::tr("%1 is missing a name.").arg(context);
        return false;
    }
    return parseValue(object, coordinate.value, error, context);
}

bool parseCoordinates(const QJsonValue& json, QVector<ResultCoordinate>& coordinates,
                      QString& error, const QString& context) {
    if (!json.isArray()) {
        error = QObject::tr("%1 must be an array.").arg(context);
        return false;
    }
    const QJsonArray array = json.toArray();
    coordinates.reserve(array.size());
    for (qsizetype index = 0; index < array.size(); ++index) {
        ResultCoordinate coordinate;
        if (!parseCoordinate(array.at(index), coordinate, error,
                             QStringLiteral("%1[%2]").arg(context).arg(index))) return false;
        coordinates.append(std::move(coordinate));
    }
    return true;
}

bool parseObservations(const QJsonValue& json, QVector<ResultObservation>& observations,
                       QString& error, const QString& context) {
    if (!json.isArray()) {
        error = QObject::tr("%1 must be an array.").arg(context);
        return false;
    }
    const QJsonArray array = json.toArray();
    observations.reserve(array.size());
    for (qsizetype index = 0; index < array.size(); ++index) {
        if (!array.at(index).isObject()) {
            error = QObject::tr("%1[%2] must be an object.").arg(context).arg(index);
            return false;
        }
        const QJsonObject object = array.at(index).toObject();
        ResultObservation observation;
        observation.name = object.value(QStringLiteral("name")).toString();
        observation.operation = object.value(QStringLiteral("operation")).toString();
        if (observation.name.isEmpty()) {
            error = QObject::tr("%1[%2] is missing a name.").arg(context).arg(index);
            return false;
        }
        if (!parseValue(object, observation.value, error,
                        QStringLiteral("%1[%2]").arg(context).arg(index))) return false;
        observations.append(std::move(observation));
    }
    return true;
}

bool parsePosition(const QJsonValue& json, ResultSourcePosition& position) {
    if (!json.isObject()) return false;
    const QJsonObject object = json.toObject();
    const double line = object.value(QStringLiteral("line")).toDouble(-1);
    const double column = object.value(QStringLiteral("column")).toDouble(-1);
    const double offset = object.value(QStringLiteral("offset")).toDouble(-1);
    if (line < 1 || column < 1 || offset < 0 || std::floor(line) != line ||
        std::floor(column) != column || std::floor(offset) != offset) return false;
    position = {static_cast<int>(line), static_cast<int>(column), static_cast<int>(offset)};
    return true;
}

bool parseAssertions(const QJsonValue& json, QVector<ResultAssertion>& assertions,
                     QString& error, const QString& context) {
    if (!json.isArray()) {
        error = QObject::tr("%1 must be an array.").arg(context);
        return false;
    }
    const QJsonArray array = json.toArray();
    assertions.reserve(array.size());
    for (qsizetype index = 0; index < array.size(); ++index) {
        const QJsonObject object = array.at(index).toObject();
        ResultAssertion assertion;
        assertion.id = object.value(QStringLiteral("id")).toString();
        assertion.comparison = object.value(QStringLiteral("operator")).toString();
        const QJsonObject source = object.value(QStringLiteral("source")).toObject();
        const QJsonValue passed = object.value(QStringLiteral("passed"));
        if (object.isEmpty() || assertion.id.isEmpty() || assertion.comparison.isEmpty() ||
            !passed.isBool() ||
            !parsePosition(source.value(QStringLiteral("begin")), assertion.begin) ||
            !parsePosition(source.value(QStringLiteral("end")), assertion.end)) {
            error = QObject::tr("%1[%2] is incomplete.").arg(context).arg(index);
            return false;
        }
        if (!parseValue(object.value(QStringLiteral("left")).toObject(), assertion.left,
                        error, context + QStringLiteral(" left operand")) ||
            !parseValue(object.value(QStringLiteral("right")).toObject(), assertion.right,
                        error, context + QStringLiteral(" right operand"))) return false;
        assertion.passed = passed.toBool();
        assertions.append(std::move(assertion));
    }
    return true;
}

bool parseCount(const QJsonObject& object, const QString& name, int& result) {
    const double value = object.value(name).toDouble(-1);
    if (value < 0 || value > static_cast<double>(std::numeric_limits<int>::max()) ||
        std::floor(value) != value) return false;
    result = static_cast<int>(value);
    return true;
}

} // namespace

QString ResultValue::displayText() const {
    if (is_boolean) return boolean_value ? QStringLiteral("true") : QStringLiteral("false");
    const QString number = QString::number(display_value, 'g', 8);
    return display_unit.isEmpty() || display_unit == QStringLiteral("none")
        ? number : QStringLiteral("%1 %2").arg(number, display_unit);
}

std::optional<ResultsModel> ResultsModel::fromJsonFile(const QString& path, QString* error) {
    const auto contents = readFile(path, error);
    return contents ? fromJson(*contents, error) : std::nullopt;
}

std::optional<ResultsModel> ResultsModel::fromJson(const QByteArray& json, QString* error) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QObject::tr("The result JSON is invalid at offset %1: %2")
                            .arg(parseError.offset).arg(parseError.errorString()));
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    const QJsonObject schema = root.value(QStringLiteral("schema")).toObject();
    if (schema.value(QStringLiteral("name")).toString() !=
        QStringLiteral("horusrf.characterization-result")) {
        setError(error, QObject::tr("The result JSON has an unknown schema."));
        return std::nullopt;
    }
    if (schema.value(QStringLiteral("version")).toInt(-1) != 1) {
        setError(error, QObject::tr("The result JSON schema version is not supported."));
        return std::nullopt;
    }

    ResultsModel model;
    model.structured_ = true;
    model.characterization_ = root.value(QStringLiteral("characterization")).toString();
    const QJsonObject qualification = root.value(QStringLiteral("qualification")).toObject();
    const QJsonObject pointCounts = qualification.value(QStringLiteral("points")).toObject();
    model.qualification_.status = qualification.value(QStringLiteral("status")).toString();
    const bool validStatus = model.qualification_.status == QStringLiteral("pass") ||
        model.qualification_.status == QStringLiteral("fail") ||
        model.qualification_.status == QStringLiteral("not_evaluated");
    if (model.characterization_.isEmpty() || !validStatus ||
        !parseCount(pointCounts, QStringLiteral("total"), model.qualification_.total_points) ||
        !parseCount(pointCounts, QStringLiteral("passed"), model.qualification_.passed_points) ||
        !parseCount(pointCounts, QStringLiteral("failed"), model.qualification_.failed_points)) {
        setError(error, QObject::tr("The result JSON qualification summary is incomplete."));
        return std::nullopt;
    }
    const QJsonValue summaryAssertions = qualification.value(QStringLiteral("assertions"));
    if (!summaryAssertions.isArray()) {
        setError(error, QObject::tr("The result JSON assertion summary must be an array."));
        return std::nullopt;
    }
    for (const QJsonValue& value : summaryAssertions.toArray()) {
        const QJsonObject object = value.toObject();
        AssertionSummary summary;
        summary.id = object.value(QStringLiteral("id")).toString();
        if (summary.id.isEmpty() || !parseCount(object, QStringLiteral("total"), summary.total) ||
            !parseCount(object, QStringLiteral("passed"), summary.passed) ||
            !parseCount(object, QStringLiteral("failed"), summary.failed) ||
            summary.passed + summary.failed != summary.total) {
            setError(error, QObject::tr("The result JSON contains an invalid assertion summary."));
            return std::nullopt;
        }
        model.qualification_.assertions.append(std::move(summary));
    }

    const QJsonValue points = root.value(QStringLiteral("points"));
    if (!points.isArray() || points.toArray().isEmpty()) {
        setError(error, QObject::tr("The result JSON contains no points."));
        return std::nullopt;
    }
    QString detail;
    const QJsonArray pointArray = points.toArray();
    for (qsizetype index = 0; index < pointArray.size(); ++index) {
        const QJsonObject object = pointArray.at(index).toObject();
        ResultPoint point;
        const QString context = QStringLiteral("points[%1]").arg(index);
        if (object.isEmpty() ||
            !parseCoordinates(object.value(QStringLiteral("coordinates")), point.coordinates,
                              detail, context + QStringLiteral(".coordinates")) ||
            !parseObservations(object.value(QStringLiteral("measurements")), point.measurements,
                               detail, context + QStringLiteral(".measurements")) ||
            !parseObservations(object.value(QStringLiteral("derived")), point.derived_values,
                               detail, context + QStringLiteral(".derived")) ||
            !parseAssertions(object.value(QStringLiteral("assertions")), point.assertions,
                             detail, context + QStringLiteral(".assertions"))) {
            setError(error, detail.isEmpty() ? QObject::tr("%1 is invalid.").arg(context) : detail);
            return std::nullopt;
        }
        point.qualification = object.value(QStringLiteral("qualification")).toString();
        if (point.qualification != QStringLiteral("pass") &&
            point.qualification != QStringLiteral("fail") &&
            point.qualification != QStringLiteral("not_evaluated")) {
            setError(error, QObject::tr("%1 is missing qualification state.").arg(context));
            return std::nullopt;
        }
        if (!model.points_.isEmpty()) {
            const ResultPoint& shape = model.points_.front();
            auto sameCoordinates = [&] {
                if (point.coordinates.size() != shape.coordinates.size()) return false;
                for (qsizetype i = 0; i < shape.coordinates.size(); ++i) {
                    if (point.coordinates.at(i).name != shape.coordinates.at(i).name ||
                        point.coordinates.at(i).value.type != shape.coordinates.at(i).value.type)
                        return false;
                }
                return true;
            };
            auto sameObservations = [](const QVector<ResultObservation>& left,
                                       const QVector<ResultObservation>& right) {
                if (left.size() != right.size()) return false;
                for (qsizetype i = 0; i < left.size(); ++i) {
                    if (left.at(i).name != right.at(i).name ||
                        left.at(i).value.type != right.at(i).value.type) return false;
                }
                return true;
            };
            if (!sameCoordinates() ||
                !sameObservations(point.measurements, shape.measurements) ||
                !sameObservations(point.derived_values, shape.derived_values) ||
                point.assertions.size() != shape.assertions.size()) {
                setError(error, QObject::tr("%1 does not match the first point shape.").arg(context));
                return std::nullopt;
            }
            for (qsizetype i = 0; i < shape.assertions.size(); ++i) {
                if (point.assertions.at(i).id != shape.assertions.at(i).id) {
                    setError(error, QObject::tr("%1 assertion order is inconsistent.").arg(context));
                    return std::nullopt;
                }
            }
        }
        model.points_.append(std::move(point));
    }
    if (model.qualification_.total_points != model.points_.size()) {
        setError(error, QObject::tr("The point total does not match the points array."));
        return std::nullopt;
    }
    if ((model.qualification_.status == QStringLiteral("pass") ||
         model.qualification_.status == QStringLiteral("fail")) &&
        model.qualification_.passed_points + model.qualification_.failed_points !=
            model.qualification_.total_points) {
        setError(error, QObject::tr("The qualification point counts are inconsistent."));
        return std::nullopt;
    }
    for (const AssertionSummary& summary : model.qualification_.assertions) {
        if (summary.total != model.qualification_.total_points) {
            setError(error, QObject::tr("An assertion total does not match the point total."));
            return std::nullopt;
        }
    }

    const QJsonValue calibrations = root.value(QStringLiteral("calibrations"));
    if (!calibrations.isArray()) {
        setError(error, QObject::tr("The result JSON calibrations member must be an array."));
        return std::nullopt;
    }
    const QJsonArray calibrationArray = calibrations.toArray();
    for (qsizetype index = 0; index < calibrationArray.size(); ++index) {
        const QJsonObject object = calibrationArray.at(index).toObject();
        CalibrationSurface surface;
        surface.name = object.value(QStringLiteral("name")).toString();
        surface.correction_type = object.value(QStringLiteral("correction_type")).toString();
        surface.canonical_unit = object.value(QStringLiteral("canonical_unit")).toString();
        if (surface.name.isEmpty() || !object.value(QStringLiteral("dimensions")).isArray() ||
            !object.value(QStringLiteral("entries")).isArray()) {
            setError(error, QObject::tr("calibrations[%1] is incomplete.").arg(index));
            return std::nullopt;
        }
        for (const QJsonValue& dimensionValue : object.value(QStringLiteral("dimensions")).toArray()) {
            const QJsonObject dimensionObject = dimensionValue.toObject();
            ResultCoordinate dimension;
            dimension.name = dimensionObject.value(QStringLiteral("name")).toString();
            dimension.value.type = dimensionObject.value(QStringLiteral("type")).toString();
            if (dimension.name.isEmpty() || dimension.value.type.isEmpty()) {
                setError(error, QObject::tr("calibrations[%1] has an invalid dimension.").arg(index));
                return std::nullopt;
            }
            surface.dimensions.append(std::move(dimension));
        }
        for (const QJsonValue& entryValue : object.value(QStringLiteral("entries")).toArray()) {
            const QJsonObject entryObject = entryValue.toObject();
            CalibrationEntry entry;
            if (!parseCoordinates(entryObject.value(QStringLiteral("coordinates")), entry.coordinates,
                                  detail, QStringLiteral("calibrations[%1].entries").arg(index)) ||
                !parseValue(entryObject.value(QStringLiteral("correction")).toObject(),
                            entry.correction, detail,
                            QStringLiteral("calibrations[%1].correction").arg(index))) {
                setError(error, detail);
                return std::nullopt;
            }
            surface.entries.append(std::move(entry));
        }
        model.calibrations_.append(std::move(surface));
    }
    return model;
}

std::optional<ResultsModel> ResultsModel::fromCsvFile(const QString& path, QString* error) {
    const auto contents = readFile(path, error);
    return contents ? fromCsv(*contents, error) : std::nullopt;
}

std::optional<ResultsModel> ResultsModel::fromCsv(const QByteArray& csv, QString* error) {
    QList<QByteArray> lines = csv.split('\n');
    if (!lines.isEmpty() && lines.back().isEmpty()) lines.removeLast();
    for (QByteArray& line : lines) if (line.endsWith('\r')) line.chop(1);
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
            setError(error, QObject::tr("Result CSV row %1 must contain exactly seven fields.").arg(row + 1));
            return std::nullopt;
        }
        std::array<double, 7> values{};
        for (qsizetype column = 0; column < fields.size(); ++column) {
            if (!parseFiniteNumber(fields.at(column), values.at(column))) {
                setError(error, QObject::tr("Result CSV row %1, field %2 is not a finite number.")
                                    .arg(row + 1).arg(column + 1));
                return std::nullopt;
            }
        }
        model.samples_.append(ResultSample{values[0], values[1], values[2], values[3],
                                           values[4], values[5], values[6]});
    }
    return model;
}

} // namespace horusrf::studio
