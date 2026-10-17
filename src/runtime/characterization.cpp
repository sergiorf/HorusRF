#include "horusrf/runtime/characterization.hpp"

#include "executor_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace horusrf::runtime {
namespace {

constexpr std::size_t maximum_sweep_points = 1'000'000;
constexpr long double sweep_tolerance_multiplier = 16.0L;

struct SweepPlan {
    std::string name;
    ir::SweepDimension dimension;
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

std::optional<SweepPlan> plan_sweep(std::string name,
                                    ir::SweepDimension dimension,
                                    long double start, long double end,
                                    long double step, parser::SourceSpan span,
                                    std::vector<Diagnostic>& diagnostics) {
    if (!std::isfinite(start) || !std::isfinite(end) || !std::isfinite(step) ||
        step <= 0.0L || end < start) {
        diagnostics.push_back(diagnostic(
            DiagnosticCode::InvalidSweep,
            "sweep requires finite bounds, a positive step, and an end not below its start",
            span));
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
                                         "sweep point count exceeds the safety limit",
                                         span));
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
                                         span));
        return std::nullopt;
    }

    return SweepPlan{std::move(name), dimension,
                     static_cast<std::size_t>(point_count), start, end, step,
                     tolerance};
}

long double value_at(const SweepPlan& sweep, std::size_t index) {
    auto candidate = sweep.start + static_cast<long double>(index) * sweep.step;
    if (std::abs(candidate - sweep.end) <= sweep.tolerance) candidate = sweep.end;
    return candidate;
}

domain::Coordinate coordinate_at(const SweepPlan& sweep, std::size_t index) {
    const auto value = static_cast<double>(value_at(sweep, index));
    if (sweep.dimension == ir::SweepDimension::Frequency) {
        return domain::Coordinate{sweep.name, domain::CoordinateType::Frequency,
                                  domain::Frequency::from_hertz(value)};
    }
    return domain::Coordinate{sweep.name, domain::CoordinateType::Temperature,
                              domain::Temperature::from_kelvin(value)};
}

domain::Frequency point_frequency(const domain::OperatingPoint& point) {
    for (const auto& coordinate : point.coordinates) {
        if (coordinate.type == domain::CoordinateType::Frequency) {
            return std::get<domain::Frequency>(coordinate.value);
        }
    }
    throw std::logic_error("validated operating point has no frequency coordinate");
}

template <typename T>
const T& slot(const PointEvaluation& evaluation, ir::ValueId id) {
    return std::get<T>(evaluation.at(id));
}

domain::QualificationValue qualification_value(const RuntimeValue& value) {
    return std::visit([](const auto& typed) -> domain::QualificationValue {
        using T = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, bool>) {
            throw std::logic_error("Boolean is not an assertion operand");
        } else {
            return typed;
        }
    }, value);
}

domain::ComparisonOperator comparison_operator(ir::ComparisonOperator op) {
    switch (op) {
    case ir::ComparisonOperator::Less: return domain::ComparisonOperator::Less;
    case ir::ComparisonOperator::LessEqual: return domain::ComparisonOperator::LessEqual;
    case ir::ComparisonOperator::Greater: return domain::ComparisonOperator::Greater;
    case ir::ComparisonOperator::GreaterEqual: return domain::ComparisonOperator::GreaterEqual;
    case ir::ComparisonOperator::Equal: return domain::ComparisonOperator::Equal;
    case ir::ComparisonOperator::NotEqual: return domain::ComparisonOperator::NotEqual;
    }
    throw std::logic_error("invalid comparison operator");
}

bool finite_value(const RuntimeValue& value) {
    return std::visit([](const auto& typed) {
        using T = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, bool>) return true;
        else if constexpr (std::is_same_v<T, domain::Frequency> ||
                           std::is_same_v<T, domain::FrequencyDelta>) return std::isfinite(typed.hertz());
        else if constexpr (std::is_same_v<T, domain::Ratio>) return std::isfinite(typed.ratio());
        else if constexpr (std::is_same_v<T, domain::Temperature> ||
                           std::is_same_v<T, domain::TemperatureDelta>) return std::isfinite(typed.kelvin());
        else if constexpr (std::is_same_v<T, domain::Power>) return std::isfinite(typed.dbm());
        else return std::isfinite(typed.db());
    }, value);
}

} // namespace

CharacterizationExecutionResult execute_characterization(const ir::Program& program,
                                                          device::RfDevice& tester,
                                                          device::MeasurementDevice& measurement) {
    return execute_characterization(program,
                                    DeviceCapabilities{tester, &measurement, nullptr});
}

CharacterizationExecutionResult execute_characterization(
    const ir::Program& program, DeviceCapabilities devices) {
    auto diagnostics = detail::validate_program(program);
    if (!diagnostics.empty()) return {std::nullopt, std::move(diagnostics)};

    const auto& calibration = program.calibration;
    if (calibration.name.empty()) {
        diagnostics.push_back(diagnostic(DiagnosticCode::InvalidCalibrationBinding,
                                         "calibration name must not be empty",
                                         calibration.span));
        return {std::nullopt, std::move(diagnostics)};
    }

    std::vector<SweepPlan> sweeps;
    sweeps.reserve(program.sweeps.size());
    std::size_t point_count = 1;
    for (std::size_t index = 0; index < program.sweeps.size(); ++index) {
        const auto& source = program.sweeps[index];
        long double start{};
        long double end{};
        long double step{};
        if (program.sweeps.size() == 1 &&
            source.dimension == ir::SweepDimension::Frequency) {
            start = program.sweep.start.hertz();
            end = program.sweep.end.hertz();
            step = program.sweep.step.hertz();
        } else if (source.dimension == ir::SweepDimension::Frequency) {
            start = std::get<domain::Frequency>(source.start).hertz();
            end = std::get<domain::Frequency>(source.end).hertz();
            step = std::get<domain::Frequency>(source.step).hertz();
        } else {
            start = std::get<domain::Temperature>(source.start).kelvin();
            end = std::get<domain::Temperature>(source.end).kelvin();
            step = std::get<domain::TemperatureDelta>(source.step).kelvin();
        }

        auto planned = plan_sweep(source.name, source.dimension, start, end, step,
                                  source.span, diagnostics);
        if (!planned) return {std::nullopt, std::move(diagnostics)};
        if (program.sweeps.size() > 1 &&
            planned->point_count != source.point_count) {
            diagnostics.push_back(diagnostic(
                DiagnosticCode::InvalidProgramBinding,
                "computed sweep count does not match the IR metadata", source.span));
            return {std::nullopt, std::move(diagnostics)};
        }
        if (point_count > maximum_sweep_points / planned->point_count) {
            diagnostics.push_back(diagnostic(
                DiagnosticCode::SweepTooLarge,
                "sweep product exceeds the safety limit", source.span));
            return {std::nullopt, std::move(diagnostics)};
        }
        point_count *= planned->point_count;
        sweeps.push_back(std::move(*planned));
    }
    if (program.sweeps.size() > 1 && point_count != program.total_point_count) {
        diagnostics.push_back(diagnostic(
            DiagnosticCode::InvalidProgramBinding,
            "computed sweep product does not match the IR metadata", program.span));
        return {std::nullopt, std::move(diagnostics)};
    }
    for (const auto& measurement : program.measurements) {
        const bool missing =
            (measurement.operation == ir::MeasurementOperation::Power && devices.power == nullptr) ||
            (measurement.operation == ir::MeasurementOperation::Evm && devices.evm == nullptr) ||
            (measurement.operation == ir::MeasurementOperation::FrequencyError &&
             devices.frequency_error == nullptr);
        if (missing) {
            const char* capability = measurement.operation == ir::MeasurementOperation::Power
                                         ? "power"
                                     : measurement.operation == ir::MeasurementOperation::Evm
                                         ? "EVM"
                                         : "frequency-error";
            diagnostics.push_back(diagnostic(
                DiagnosticCode::CapabilityUnavailable,
                std::string{capability} + " measurement capability is required",
                measurement.span));
            return {std::nullopt, std::move(diagnostics)};
        }
    }
    for (const auto& sweep : program.sweeps) {
        if (sweep.dimension == ir::SweepDimension::Temperature &&
            devices.temperature == nullptr) {
            diagnostics.push_back(diagnostic(
                DiagnosticCode::CapabilityUnavailable,
                "temperature control capability is required", sweep.span));
            return {std::nullopt, std::move(diagnostics)};
        }
    }

    domain::CharacterizationResult result;
    result.samples.reserve(point_count);
    result.calibration.name = calibration.name;
    result.calibration.dimensions.reserve(point_count);
    result.calibration.corrections.reserve(point_count);
    result.calibration.operating_points.reserve(point_count);
    result.qualification.total_points = point_count;
    result.qualification.assertions.reserve(program.assertions.size());
    for (const auto& assertion : program.assertions) {
        result.qualification.assertions.push_back(
            domain::AssertionSummary{assertion.id, point_count, 0, 0});
    }

    for (std::size_t flat_index = 0; flat_index < point_count; ++flat_index) {
        domain::OperatingPoint point;
        std::vector<std::size_t> axis_indexes(sweeps.size());
        auto remainder = flat_index;
        for (std::size_t reverse = sweeps.size(); reverse-- > 0;) {
            axis_indexes[reverse] = remainder % sweeps[reverse].point_count;
            remainder /= sweeps[reverse].point_count;
        }
        point.coordinates.reserve(sweeps.size());
        for (std::size_t axis = 0; axis < sweeps.size(); ++axis) {
            point.coordinates.push_back(coordinate_at(sweeps[axis],
                                                       axis_indexes[axis]));
        }
        const auto frequency = point_frequency(point);
        const auto evaluation = detail::evaluate_validated_point(
            program, std::move(point), devices);
        for (std::size_t value_index = 0; value_index < evaluation.values.size();
             ++value_index) {
            if (!finite_value(evaluation.values[value_index])) {
                diagnostics.push_back(diagnostic(
                    DiagnosticCode::NonFiniteValue, "runtime value is not finite",
                    program.values[value_index].span));
                return {std::nullopt, std::move(diagnostics)};
            }
        }
        const auto reference = slot<domain::Power>(evaluation, program.reference);
        const auto measured = slot<domain::Power>(evaluation, program.measurement);
        const auto correction =
            slot<domain::PowerDelta>(evaluation, calibration.correction);

        auto sample = domain::make_characterization_sample(
            evaluation.operating_point, frequency, reference, measured, correction);
        sample.observations.reserve(program.measurements.size());
        for (const auto& measurement : program.measurements) {
            if (measurement.type == ir::ValueType::Power) {
                sample.observations.push_back(domain::NamedObservation{
                    measurement.name, domain::ObservationType::Power,
                    slot<domain::Power>(evaluation, measurement.value), "dBm"});
            } else if (measurement.type == ir::ValueType::Ratio) {
                sample.observations.push_back(domain::NamedObservation{
                    measurement.name, domain::ObservationType::Ratio,
                    slot<domain::Ratio>(evaluation, measurement.value), "ratio"});
            } else {
                sample.observations.push_back(domain::NamedObservation{
                    measurement.name, domain::ObservationType::FrequencyDelta,
                    slot<domain::FrequencyDelta>(evaluation, measurement.value), "Hz"});
            }
        }
        sample.assertions.reserve(program.assertions.size());
        bool point_passed = true;
        for (std::size_t index = 0; index < program.assertions.size(); ++index) {
            const auto& assertion = program.assertions[index];
            const bool passed = slot<bool>(evaluation, assertion.value);
            point_passed = point_passed && passed;
            auto& summary = result.qualification.assertions[index];
            if (passed) ++summary.passed;
            else ++summary.failed;
            sample.assertions.push_back(domain::AssertionOutcome{
                assertion.id,
                domain::ResultSourceSpan{
                    {assertion.span.begin.line, assertion.span.begin.column,
                     assertion.span.begin.offset},
                    {assertion.span.end.line, assertion.span.end.column,
                     assertion.span.end.offset}},
                evaluation.operating_point,
                comparison_operator(assertion.op),
                qualification_value(evaluation.at(assertion.left)),
                qualification_value(evaluation.at(assertion.right)), passed});
        }
        if (program.assertions.empty()) {
            sample.qualification = domain::QualificationStatus::NotEvaluated;
        } else if (point_passed) {
            sample.qualification = domain::QualificationStatus::Pass;
            ++result.qualification.passed_points;
        } else {
            sample.qualification = domain::QualificationStatus::Fail;
            ++result.qualification.failed_points;
        }
        result.samples.push_back(std::move(sample));
        result.calibration.dimensions.push_back(frequency);
        result.calibration.corrections.push_back(correction);
        result.calibration.operating_points.push_back(evaluation.operating_point);
    }

    result.metrics = domain::calculate_metrics(result.samples);
    result.qualification.status = program.assertions.empty()
                                      ? domain::QualificationStatus::NotEvaluated
                                      : result.qualification.failed_points == 0
                                            ? domain::QualificationStatus::Pass
                                            : domain::QualificationStatus::Fail;
    return {std::move(result), {}};
}

} // namespace horusrf::runtime
