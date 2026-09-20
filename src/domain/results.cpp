#include "horusrf/domain/results.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace horusrf::domain {

CharacterizationMetrics calculate_metrics(
    std::span<const CharacterizationSample> samples) {
    if (samples.empty()) {
        throw std::invalid_argument("characterization metrics require at least one sample");
    }

    long double uncorrected_square_sum{};
    long double corrected_square_sum{};
    double maximum_absolute_error{};
    double corrected_maximum_absolute_error{};

    for (const auto& sample : samples) {
        const auto error = static_cast<long double>(sample.error.db());
        const auto residual = static_cast<long double>(sample.residual_error.db());
        uncorrected_square_sum += error * error;
        corrected_square_sum += residual * residual;
        maximum_absolute_error =
            std::max(maximum_absolute_error, std::abs(sample.error.db()));
        corrected_maximum_absolute_error = std::max(
            corrected_maximum_absolute_error, std::abs(sample.residual_error.db()));
    }

    const auto count = static_cast<long double>(samples.size());
    const auto uncorrected_rms =
        static_cast<double>(std::sqrt(uncorrected_square_sum / count));
    const auto corrected_rms =
        static_cast<double>(std::sqrt(corrected_square_sum / count));

    double improvement = 1.0;
    if (corrected_rms != 0.0) {
        improvement = uncorrected_rms / corrected_rms;
    } else if (uncorrected_rms != 0.0) {
        improvement = std::numeric_limits<double>::infinity();
    }

    return CharacterizationMetrics{uncorrected_rms,
                                   corrected_rms,
                                   maximum_absolute_error,
                                   corrected_maximum_absolute_error,
                                   improvement};
}

} // namespace horusrf::domain
