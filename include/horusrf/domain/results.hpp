#pragma once

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
