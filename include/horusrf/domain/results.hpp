#pragma once

#include <span>
#include <string>
#include <vector>

#include "horusrf/domain/quantity.hpp"

namespace horusrf::domain {

struct CharacterizationSample {
    Frequency frequency;
    Power reference_power;
    Power measured_power;
    PowerDelta error;
    PowerDelta correction;
    Power corrected_power;
    PowerDelta residual_error;
};

struct CalibrationArtifact {
    std::string name;
    std::vector<Frequency> dimensions;
    std::vector<PowerDelta> corrections;
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

[[nodiscard]] CharacterizationMetrics calculate_metrics(
    std::span<const CharacterizationSample> samples);

} // namespace horusrf::domain
