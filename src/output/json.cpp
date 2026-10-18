#include "horusrf/output/json.hpp"

#include <cmath>
#include <iomanip>
#include <ios>
#include <limits>
#include <locale>
#include <ostream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <variant>

namespace horusrf::output {
namespace {

class StreamState {
public:
    explicit StreamState(std::ostream& stream)
        : stream_(stream), flags_(stream.flags()), precision_(stream.precision()),
          locale_(stream.getloc()) {}
    ~StreamState() {
        try {
            stream_.imbue(locale_);
            stream_.flags(flags_);
            stream_.precision(precision_);
        } catch (...) {
        }
    }
private:
    std::ostream& stream_;
    std::ios::fmtflags flags_;
    std::streamsize precision_;
    std::locale locale_;
};

void string(std::ostream& output, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    output.put('"');
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (ch < 0x20) {
                output << "\\u00" << hex[ch >> 4] << hex[ch & 0x0f];
            } else {
                output.put(static_cast<char>(ch));
            }
        }
    }
    output.put('"');
}

void number(std::ostream& output, double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument("JSON result contains a non-finite number");
    }
    output << value;
}

const char* status(domain::QualificationStatus value) {
    switch (value) {
    case domain::QualificationStatus::Pass: return "pass";
    case domain::QualificationStatus::Fail: return "fail";
    case domain::QualificationStatus::NotEvaluated: return "not_evaluated";
    }
    throw std::invalid_argument("invalid qualification status");
}

const char* comparison(domain::ComparisonOperator value) {
    switch (value) {
    case domain::ComparisonOperator::Less: return "<";
    case domain::ComparisonOperator::LessEqual: return "<=";
    case domain::ComparisonOperator::Greater: return ">";
    case domain::ComparisonOperator::GreaterEqual: return ">=";
    case domain::ComparisonOperator::Equal: return "==";
    case domain::ComparisonOperator::NotEqual: return "!=";
    }
    throw std::invalid_argument("invalid comparison operator");
}

struct NumericMetadata {
    const char* type;
    double canonical;
    const char* canonical_unit;
    double display;
    const char* display_unit;
};

template <typename Value>
NumericMetadata metadata(Value value) {
    if constexpr (std::is_same_v<Value, domain::Frequency>) {
        return {"Frequency", value.hertz(), "Hz", value.hertz() / 1.0e9, "GHz"};
    } else if constexpr (std::is_same_v<Value, domain::FrequencyDelta>) {
        return {"FrequencyDelta", value.hertz(), "Hz", value.hertz() / 1.0e3, "kHz"};
    } else if constexpr (std::is_same_v<Value, domain::Power>) {
        return {"Power", value.dbm(), "dBm", value.dbm(), "dBm"};
    } else if constexpr (std::is_same_v<Value, domain::PowerDelta>) {
        return {"PowerDelta", value.db(), "dB", value.db(), "dB"};
    } else if constexpr (std::is_same_v<Value, domain::Ratio>) {
        return {"Ratio", value.ratio(), "ratio", value.ratio() * 100.0, "%"};
    } else if constexpr (std::is_same_v<Value, domain::Temperature>) {
        return {"Temperature", value.kelvin(), "K", value.kelvin() - 273.15, "degC"};
    } else {
        return {"TemperatureDelta", value.kelvin(), "K", value.kelvin(), "deltaC"};
    }
}

NumericMetadata coordinate_metadata(const domain::Coordinate& coordinate) {
    auto info = std::visit([](const auto& value) { return metadata(value); },
                           coordinate.value);
    if (coordinate.display_unit.empty()) return info;
    if (coordinate.type == domain::CoordinateType::Frequency) {
        const auto hertz = info.canonical;
        if (coordinate.display_unit == "Hz") info.display = hertz;
        else if (coordinate.display_unit == "kHz") info.display = hertz / 1.0e3;
        else if (coordinate.display_unit == "MHz") info.display = hertz / 1.0e6;
        else if (coordinate.display_unit == "GHz") info.display = hertz / 1.0e9;
        else throw std::invalid_argument("invalid frequency display unit");
    } else if (coordinate.display_unit == "K") {
        info.display = info.canonical;
    } else if (coordinate.display_unit == "degC") {
        info.display = info.canonical - 273.15;
    } else {
        throw std::invalid_argument("invalid temperature display unit");
    }
    info.display_unit = coordinate.display_unit.c_str();
    return info;
}

template <typename Variant>
void typed_fields(std::ostream& output, const Variant& value) {
    std::visit([&](const auto& typed) {
        using T = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, bool>) {
            output << "\"type\":\"Boolean\",\"value\":"
                   << (typed ? "true" : "false");
        } else {
            const auto info = metadata(typed);
            output << "\"type\":";
            string(output, info.type);
            output << ",\"value\":";
            number(output, info.canonical);
            output << ",\"canonical_unit\":";
            string(output, info.canonical_unit);
            output << ",\"display_value\":";
            number(output, info.display);
            output << ",\"display_unit\":";
            string(output, info.display_unit);
        }
    }, value);
}

template <typename Variant>
void typed_value(std::ostream& output, const Variant& value) {
    output << '{';
    typed_fields(output, value);
    output << '}';
}

void coordinate(std::ostream& output, const domain::Coordinate& coordinate) {
    output << "{\"name\":";
    string(output, coordinate.name);
    output << ',';
    const auto rendered = coordinate_metadata(coordinate);
    output << "\"type\":";
    string(output, rendered.type);
    output << ",\"value\":";
    number(output, rendered.canonical);
    output << ",\"canonical_unit\":";
    string(output, rendered.canonical_unit);
    output << ",\"display_value\":";
    number(output, rendered.display);
    output << ",\"display_unit\":";
    string(output, rendered.display_unit);
    output << '}';
}

void coordinates(std::ostream& output, const domain::OperatingPoint& point) {
    output << '[';
    for (std::size_t i = 0; i < point.coordinates.size(); ++i) {
        if (i != 0) output << ',';
        coordinate(output, point.coordinates[i]);
    }
    output << ']';
}

void source_position(std::ostream& output, const domain::ResultSourcePosition& position) {
    output << "{\"line\":" << position.line << ",\"column\":" << position.column
           << ",\"offset\":" << position.offset << '}';
}

void qualification(std::ostream& output, const domain::QualificationSummary& summary) {
    output << "{\"status\":";
    string(output, status(summary.status));
    output << ",\"points\":{\"total\":" << summary.total_points
           << ",\"passed\":" << summary.passed_points
           << ",\"failed\":" << summary.failed_points << "},\"assertions\":[";
    for (std::size_t i = 0; i < summary.assertions.size(); ++i) {
        if (i != 0) output << ',';
        const auto& assertion = summary.assertions[i];
        output << "{\"id\":";
        string(output, assertion.id);
        output << ",\"total\":" << assertion.total << ",\"passed\":"
               << assertion.passed << ",\"failed\":" << assertion.failed << '}';
    }
    output << "]}";
}

} // namespace

void write_json(std::ostream& output, const domain::CharacterizationResult& result) {
    StreamState state(output);
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10)
           << std::defaultfloat;

    output << "{\"schema\":{\"name\":\"horusrf.characterization-result\",\"version\":1},"
              "\"characterization\":";
    string(output, result.characterization_name);
    output << ",\"qualification\":";
    qualification(output, result.qualification);
    output << ",\"points\":[";
    for (std::size_t point_index = 0; point_index < result.samples.size(); ++point_index) {
        if (point_index != 0) output << ',';
        const auto& sample = result.samples[point_index];
        output << "{\"coordinates\":";
        coordinates(output, sample.operating_point);
        output << ",\"measurements\":[";
        for (std::size_t i = 0; i < sample.observations.size(); ++i) {
            if (i != 0) output << ',';
            const auto& observation = sample.observations[i];
            output << "{\"name\":";
            string(output, observation.name);
            output << ",\"operation\":";
            string(output, observation.operation);
            output << ',';
            std::visit([&](const auto& value) {
                const auto info = metadata(value);
                output << "\"type\":";
                string(output, info.type);
                output << ",\"value\":";
                number(output, info.canonical);
                output << ",\"canonical_unit\":";
                string(output, info.canonical_unit);
                output << ",\"display_value\":";
                number(output, info.display);
                output << ",\"display_unit\":";
                string(output, info.display_unit);
            }, observation.value);
            output << '}';
        }
        output << "],\"derived\":[";
        for (std::size_t i = 0; i < sample.derived_values.size(); ++i) {
            if (i != 0) output << ',';
            const auto& derived = sample.derived_values[i];
            output << "{\"name\":";
            string(output, derived.name);
            output << ',';
            typed_fields(output, derived.value);
            output << '}';
        }
        output << "],\"assertions\":[";
        for (std::size_t i = 0; i < sample.assertions.size(); ++i) {
            if (i != 0) output << ',';
            const auto& assertion = sample.assertions[i];
            output << "{\"id\":";
            string(output, assertion.id);
            output << ",\"source\":{\"begin\":";
            source_position(output, assertion.source.begin);
            output << ",\"end\":";
            source_position(output, assertion.source.end);
            output << "},\"operator\":";
            string(output, comparison(assertion.comparison));
            output << ",\"left\":";
            typed_value(output, assertion.left);
            output << ",\"right\":";
            typed_value(output, assertion.right);
            output << ",\"passed\":" << (assertion.passed ? "true" : "false") << '}';
        }
        output << "],\"qualification\":";
        string(output, status(sample.qualification));
        output << '}';
    }
    output << "],\"calibrations\":[{\"name\":";
    string(output, result.calibration.name);
    output << ",\"dimensions\":[";
    for (std::size_t i = 0; i < result.calibration.dimension_descriptors.size(); ++i) {
        if (i != 0) output << ',';
        const auto& dimension = result.calibration.dimension_descriptors[i];
        output << "{\"name\":";
        string(output, dimension.name);
        output << ",\"type\":";
        string(output, dimension.type == domain::CoordinateType::Frequency
                           ? "Frequency" : "Temperature");
        output << '}';
    }
    output << "],\"correction_type\":\"PowerDelta\",\"canonical_unit\":\"dB\",\"entries\":[";
    for (std::size_t i = 0; i < result.calibration.entries.size(); ++i) {
        if (i != 0) output << ',';
        const auto& entry = result.calibration.entries[i];
        output << "{\"coordinates\":";
        coordinates(output, entry.operating_point);
        output << ",\"correction\":";
        const domain::ResultValue correction = entry.correction;
        typed_value(output, correction);
        output << '}';
    }
    output << "]}],\"metrics\":{\"calibration_fit\":{\"uncorrected_rms_error_db\":";
    number(output, result.metrics.uncorrected_rms_error_db);
    output << ",\"corrected_rms_error_db\":";
    number(output, result.metrics.corrected_rms_error_db);
    output << ",\"maximum_absolute_error_db\":";
    number(output, result.metrics.maximum_absolute_error_db);
    output << ",\"corrected_maximum_absolute_error_db\":";
    number(output, result.metrics.corrected_maximum_absolute_error_db);
    output << ",\"rms_improvement_ratio\":";
    if (result.metrics.corrected_rms_error_db == 0.0) {
        output << "null,\"rms_improvement_reason\":\"perfect_fit\"";
    } else {
        number(output, result.metrics.rms_improvement_ratio);
        output << ",\"rms_improvement_reason\":null";
    }
    output << "},\"qualification\":{\"status\":";
    string(output, status(result.qualification.status));
    output << ",\"pass_ratio\":";
    if (result.qualification.status == domain::QualificationStatus::NotEvaluated ||
        result.qualification.total_points == 0) {
        output << "null";
    } else {
        number(output, static_cast<double>(result.qualification.passed_points) /
                           static_cast<double>(result.qualification.total_points));
    }
    output << "},\"validation\":null}}\n";
    if (!output) throw std::ios_base::failure("failed to write characterization JSON");
}

} // namespace horusrf::output
