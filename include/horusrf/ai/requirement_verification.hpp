#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "horusrf/domain/quantity.hpp"
#include "horusrf/ir/ir.hpp"

namespace horusrf::ai {

struct SweepFact final {
    ir::SweepDimension dimension{ir::SweepDimension::Frequency};
    ir::SweepEndpoint start;
    ir::SweepEndpoint end;
    ir::SweepStep step;
    std::size_t point_count{};
    std::size_t order{};
};

struct MeasurementFact final {
    std::string name;
    ir::MeasurementOperation operation{ir::MeasurementOperation::Power};
    ir::ValueType type{ir::ValueType::Power};
    bool legacy{};
};

struct DerivedFact final {
    std::string name;
    ir::ValueType type{ir::ValueType::PowerDelta};
    std::string expression;
};

struct AssertionFact final {
    std::size_t ordinal{};
    ir::ComparisonOperator op{ir::ComparisonOperator::Equal};
    ir::ValueType operand_type{ir::ValueType::PowerDelta};
    std::string left;
    std::string right;
};

struct CalibrationFact final {
    std::string name;
    std::string correction;
    std::vector<ir::SweepDimension> indexes;
};

struct ProgramFacts final {
    domain::Power reference_power{domain::Power::from_dbm(0.0)};
    std::vector<SweepFact> sweeps;
    std::vector<MeasurementFact> measurements;
    std::vector<DerivedFact> derived_values;
    std::vector<AssertionFact> assertions;
    CalibrationFact calibration;
};

// Produces a stable compiler-IR expression identity. Aliases are expanded and
// linear delta arithmetic is canonicalized, so -(measured-reference) and
// reference-measured have the same identity.
[[nodiscard]] std::optional<std::string> normalize_expression(
    const ir::Program& program, ir::ValueId value);

[[nodiscard]] std::optional<ProgramFacts> project_program_facts(
    const ir::Program& program);
[[nodiscard]] std::optional<ProgramFacts> compile_program_facts(
    std::string_view source);

enum class RequirementKind {
    ReferencePower,
    Sweep,
    Measurement,
    Assertion,
    Calibration,
};

struct RequirementClause final {
    std::string id;
    RequirementKind kind{RequirementKind::ReferencePower};
    std::string provenance{"curated"};
    std::optional<domain::Power> reference_power;
    std::optional<SweepFact> sweep;
    std::optional<MeasurementFact> measurement;
    std::optional<AssertionFact> assertion;
    std::optional<CalibrationFact> calibration;
    // A missing one-point step means the representation's positive step was
    // not explicitly requested and must not affect satisfaction.
    bool constrain_sweep_step{true};
    bool constrain_sweep_order{true};
    bool constrain_measurement_name{false};
};

enum class RequirementOutcomeStatus { Satisfied, Violated, Unknown, Unsupported };
enum class RequirementSatisfactionStatus {
    Satisfied,
    Violated,
    Unknown,
    Unsupported,
    NotEvaluated,
};

struct RequirementOutcome final {
    std::string requirement_id;
    RequirementOutcomeStatus status{RequirementOutcomeStatus::Unknown};
    std::string code;
    std::string expected;
    std::string actual;
    std::string explanation;
};

struct RequirementVerificationResult final {
    RequirementSatisfactionStatus status{
        RequirementSatisfactionStatus::NotEvaluated};
    std::vector<RequirementOutcome> outcomes;

    [[nodiscard]] bool satisfied() const noexcept {
        return status == RequirementSatisfactionStatus::Satisfied;
    }
};

[[nodiscard]] RequirementVerificationResult verify_requirements(
    const std::vector<RequirementClause>& requirements,
    const ProgramFacts& facts);

} // namespace horusrf::ai
