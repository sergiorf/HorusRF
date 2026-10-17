#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "horusrf/domain/operating_point.hpp"
#include "horusrf/domain/quantity.hpp"

namespace horusrf::domain {

enum class ObservationType { Power, Ratio, FrequencyDelta };
using ObservationValue = std::variant<Power, Ratio, FrequencyDelta>;
struct NamedObservation {
    std::string name;
    ObservationType type{ObservationType::Power};
    ObservationValue value;
    std::string canonical_unit;
};

enum class ComparisonOperator { Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual };
struct ResultSourcePosition {
    std::size_t line{1};
    std::size_t column{1};
    std::size_t offset{};
    friend bool operator==(const ResultSourcePosition&, const ResultSourcePosition&) = default;
};
struct ResultSourceSpan {
    ResultSourcePosition begin;
    ResultSourcePosition end;
    friend bool operator==(const ResultSourceSpan&, const ResultSourceSpan&) = default;
};
using QualificationValue = std::variant<Frequency, FrequencyDelta, Ratio, Temperature,
                                        TemperatureDelta, Power, PowerDelta>;
enum class QualificationStatus { Pass, Fail, NotEvaluated };

struct AssertionOutcome {
    std::string id;
    ResultSourceSpan source;
    OperatingPoint operating_point;
    ComparisonOperator comparison{ComparisonOperator::Equal};
    QualificationValue left;
    QualificationValue right;
    bool passed{};
};

struct AssertionSummary {
    std::string id;
    std::size_t total{};
    std::size_t passed{};
    std::size_t failed{};
};

struct QualificationSummary {
    QualificationStatus status{QualificationStatus::NotEvaluated};
    std::size_t total_points{};
    std::size_t passed_points{};
    std::size_t failed_points{};
    std::vector<AssertionSummary> assertions;
};

struct CharacterizationSample {
    Frequency frequency;
    Power reference_power;
    Power measured_power;
    PowerDelta error;
    PowerDelta correction;
    Power corrected_power;
    PowerDelta residual_error;
    OperatingPoint operating_point;
    std::vector<NamedObservation> observations;
    std::vector<AssertionOutcome> assertions;
    QualificationStatus qualification{QualificationStatus::NotEvaluated};
};

struct CalibrationArtifact {
    std::string name;
    std::vector<Frequency> dimensions;
    std::vector<PowerDelta> corrections;
    std::vector<OperatingPoint> operating_points;
};

struct CharacterizationMetrics {
    double uncorrected_rms_error_db{};
    double corrected_rms_error_db{};
    double maximum_absolute_error_db{};
    double corrected_maximum_absolute_error_db{};
    double rms_improvement_ratio{};
};

struct CharacterizationResult {
    std::vector<CharacterizationSample> samples;
    CalibrationArtifact calibration;
    CharacterizationMetrics metrics;
    QualificationSummary qualification;
};

[[nodiscard]] CharacterizationSample make_characterization_sample(
    Frequency frequency, Power reference_power, Power measured_power,
    PowerDelta correction);

[[nodiscard]] CharacterizationSample make_characterization_sample(
    OperatingPoint operating_point, Frequency frequency, Power reference_power,
    Power measured_power, PowerDelta correction);

[[nodiscard]] CharacterizationMetrics calculate_metrics(
    std::span<const CharacterizationSample> samples);

} // namespace horusrf::domain
