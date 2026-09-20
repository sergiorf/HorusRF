#include "horusrf/runtime/characterization.hpp"

#include "executor_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace horusrf::runtime {
namespace {

constexpr std::size_t maximum_sweep_points = 1'000'000;
constexpr long double sweep_tolerance_multiplier = 16.0L;

struct SweepPlan {
    std::size_t point_count;
    long double start;
    long double end;
    long double step;
    long double tolerance;
};

Diagnostic diagnostic(DiagnosticCode code, std::string message,
                      parser::SourceSpan span) {
    return Diagnostic{code, std::move(message), span};
}

std::optional<SweepPlan> plan_sweep(const ir::Sweep& sweep,
                                    std::vector<Diagnostic>& diagnostics) {
    const auto start = static_cast<long double>(sweep.start.hertz());
    const auto end = static_cast<long double>(sweep.end.hertz());
    const auto step = static_cast<long double>(sweep.step.hertz());

    if (!std::isfinite(start) || !std::isfinite(end) || !std::isfinite(step) ||
        step <= 0.0L || end < start) {
        diagnostics.push_back(diagnostic(
            DiagnosticCode::InvalidSweep,
            "sweep requires finite bounds, a positive step, and an end not below its start",
            sweep.span));
        return std::nullopt;
    }

    const auto magnitude =
        std::max({std::abs(start), std::abs(end), std::abs(step), 1.0L});
    const auto tolerance = sweep_tolerance_multiplier *
                           static_cast<long double>(std::numeric_limits<double>::epsilon()) *
                           magnitude;
    const auto quotient = (end - start) / step;
    if (!std::isfinite(quotient) ||
        quotient > static_cast<long double>(maximum_sweep_points)) {
        diagnostics.push_back(diagnostic(DiagnosticCode::SweepTooLarge,
                                         "sweep point count exceeds the M0 safety limit",
                                         sweep.span));
        return std::nullopt;
    }

    auto last_index = std::floor(quotient);
    // A quotient just below an integer can result from the source values' binary
    // representation. Include the next lattice point only when its independently
    // calculated value is within the endpoint tolerance.
    const auto current_candidate = start + last_index * step;
    if (std::abs(current_candidate - end) > tolerance) {
        const auto next_index = last_index + 1.0L;
        const auto next_candidate = start + next_index * step;
        if (std::isfinite(next_candidate) && next_candidate <= end + tolerance) {
            last_index = next_index;
        }
    }

    const auto point_count = last_index + 1.0L;
    if (!std::isfinite(point_count) || point_count < 1.0L ||
        point_count > static_cast<long double>(maximum_sweep_points) ||
        point_count > static_cast<long double>(std::numeric_limits<std::size_t>::max())) {
        diagnostics.push_back(diagnostic(DiagnosticCode::SweepTooLarge,
                                         "sweep point count cannot be represented safely",
                                         sweep.span));
        return std::nullopt;
    }

    return SweepPlan{static_cast<std::size_t>(point_count), start, end, step,
                     tolerance};
}

domain::Frequency frequency_at(const SweepPlan& sweep, std::size_t index) {
    auto candidate = sweep.start + static_cast<long double>(index) * sweep.step;
    if (std::abs(candidate - sweep.end) <= sweep.tolerance) candidate = sweep.end;
    return domain::Frequency::from_hertz(static_cast<double>(candidate));
}

template <typename T>
const T& slot(const PointEvaluation& evaluation, ir::ValueId id) {
    return std::get<T>(evaluation.at(id));
}

} // namespace

CharacterizationExecutionResult execute_characterization(const ir::Program& program,
                                                          device::RfDevice& device) {
    auto diagnostics = detail::validate_program(program);
    if (!diagnostics.empty()) return {std::nullopt, std::move(diagnostics)};

    if (program.calibrations.size() != 1) {
        diagnostics.push_back(diagnostic(
            DiagnosticCode::InvalidCalibrationBinding,
            "complete characterization requires exactly one calibration",
            program.span));
        return {std::nullopt, std::move(diagnostics)};
    }
    const auto& calibration = program.calibrations.front();
    if (calibration.name.empty()) {
        diagnostics.push_back(diagnostic(DiagnosticCode::InvalidCalibrationBinding,
                                         "calibration name must not be empty",
                                         calibration.span));
        return {std::nullopt, std::move(diagnostics)};
    }

    const auto sweep = plan_sweep(program.sweep, diagnostics);
    if (!sweep) return {std::nullopt, std::move(diagnostics)};

    domain::CharacterizationResult result;
    result.samples.reserve(sweep->point_count);
    result.calibration.name = calibration.name;
    result.calibration.dimensions.reserve(sweep->point_count);
    result.calibration.corrections.reserve(sweep->point_count);

    for (std::size_t index = 0; index < sweep->point_count; ++index) {
        const auto frequency = frequency_at(*sweep, index);
        const auto evaluation =
            detail::evaluate_validated_point(program, frequency, device);
        const auto reference = slot<domain::Power>(evaluation, program.reference);
        const auto measured = slot<domain::Power>(evaluation, program.measurement);
        const auto error = measured - reference;
        const auto correction =
            slot<domain::PowerDelta>(evaluation, calibration.correction);
        const auto corrected = measured + correction;
        const auto residual = corrected - reference;

        result.samples.push_back(domain::CharacterizationSample{
            frequency, reference, measured, error, correction, corrected, residual});
        result.calibration.dimensions.push_back(frequency);
        result.calibration.corrections.push_back(correction);
    }

    result.metrics = domain::calculate_metrics(result.samples);
    return {std::move(result), {}};
}

} // namespace horusrf::runtime
