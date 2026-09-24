#include "horusrf/domain/results.hpp"

#include "test_support.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace horusrf::domain;

CharacterizationSample sample(double error, double residual) {
    const auto reference = Power::from_dbm(-10.0);
    const auto measured = reference + PowerDelta::from_db(error);
    const auto correction = PowerDelta::from_db(residual - error);
    return make_characterization_sample(Frequency::from_hertz(1.0), reference,
                                        measured, correction);
}

void standard_report_calculation_test() {
    const auto result = make_characterization_sample(
        Frequency::from_hertz(2.45e9), Power::from_dbm(-10.0),
        Power::from_dbm(-9.5), PowerDelta::from_db(-0.25));

    CHECK_NEAR(result.error.db(), 0.5, 1e-12);
    CHECK_NEAR(result.correction.db(), -0.25, 1e-12);
    CHECK_NEAR(result.corrected_power.dbm(), -9.75, 1e-12);
    CHECK_NEAR(result.residual_error.db(), 0.25, 1e-12);
}

void hand_checked_metrics_test() {
    const std::vector samples{sample(3.0, -1.0), sample(-4.0, 2.0)};
    const auto metrics = calculate_metrics(samples);
    CHECK_NEAR(metrics.uncorrected_rms_error_db, std::sqrt(12.5), 1e-12);
    CHECK_NEAR(metrics.corrected_rms_error_db, std::sqrt(2.5), 1e-12);
    CHECK_NEAR(metrics.maximum_absolute_error_db, 4.0, 1e-12);
    CHECK_NEAR(metrics.corrected_maximum_absolute_error_db, 2.0, 1e-12);
    CHECK_NEAR(metrics.rms_improvement_ratio, std::sqrt(5.0), 1e-12);
}

void ratio_edge_cases_test() {
    const std::vector perfect{sample(-2.0, 0.0), sample(2.0, 0.0)};
    CHECK(std::isinf(calculate_metrics(perfect).rms_improvement_ratio));

    const std::vector unchanged{sample(0.0, 0.0)};
    CHECK_EQ(calculate_metrics(unchanged).rms_improvement_ratio, 1.0);

    try {
        (void)calculate_metrics({});
        CHECK(false);
    } catch (const std::invalid_argument&) {
    }
}

void results_own_values_test() {
    CharacterizationResult result;
    {
        std::vector temporary_samples{sample(1.0, 0.25)};
        std::vector temporary_dimensions{Frequency::from_hertz(42.0)};
        std::vector temporary_corrections{PowerDelta::from_db(-0.75)};
        result.samples = temporary_samples;
        result.calibration =
            CalibrationArtifact{"owned", temporary_dimensions, temporary_corrections};
    }
    CHECK_EQ(result.samples.size(), std::size_t{1});
    CHECK_NEAR(result.samples.front().error.db(), 1.0, 1e-12);
    CHECK_EQ(result.calibration.name, std::string{"owned"});
    CHECK_NEAR(result.calibration.dimensions.front().hertz(), 42.0, 1e-12);
    CHECK_NEAR(result.calibration.corrections.front().db(), -0.75, 1e-12);
}

} // namespace

void run_results_tests() {
    standard_report_calculation_test();
    hand_checked_metrics_test();
    ratio_edge_cases_test();
    results_own_values_test();
}
