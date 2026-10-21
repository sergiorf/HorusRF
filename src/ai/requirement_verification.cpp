#include "horusrf/ai/requirement_verification.hpp"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <type_traits>
#include <utility>
#include <variant>

#include "horusrf/frontend/language_services.hpp"
#include "horusrf/ir/lowering.hpp"

namespace horusrf::ai {
namespace {

std::string number(double value) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(17) << value;
    return output.str();
}

std::string dimension_name(ir::SweepDimension value) {
    return value == ir::SweepDimension::Frequency ? "frequency" : "temperature";
}

std::string operation_name(ir::MeasurementOperation value) {
    switch (value) {
    case ir::MeasurementOperation::Power: return "power";
    case ir::MeasurementOperation::Evm: return "evm";
    case ir::MeasurementOperation::FrequencyError: return "frequency_error";
    }
    return "unknown";
}

std::string operator_name(ir::ComparisonOperator value) {
    switch (value) {
    case ir::ComparisonOperator::Less: return "less";
    case ir::ComparisonOperator::LessEqual: return "less_equal";
    case ir::ComparisonOperator::Greater: return "greater";
    case ir::ComparisonOperator::GreaterEqual: return "greater_equal";
    case ir::ComparisonOperator::Equal: return "equal";
    case ir::ComparisonOperator::NotEqual: return "not_equal";
    }
    return "unknown";
}

std::string constant(const ir::ConstantValue& input) {
    return std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, domain::Frequency>)
            return std::string{"frequency:"} + number(value.hertz());
        else if constexpr (std::is_same_v<T, domain::FrequencyDelta>)
            return std::string{"frequency_delta:"} + number(value.hertz());
        else if constexpr (std::is_same_v<T, domain::Ratio>)
            return std::string{"ratio:"} + number(value.ratio());
        else if constexpr (std::is_same_v<T, domain::Temperature>)
            return std::string{"temperature:"} + number(value.kelvin());
        else if constexpr (std::is_same_v<T, domain::TemperatureDelta>)
            return std::string{"temperature_delta:"} + number(value.kelvin());
        else if constexpr (std::is_same_v<T, domain::Power>)
            return std::string{"power:"} + number(value.dbm());
        else if constexpr (std::is_same_v<T, domain::PowerDelta>)
            return std::string{"power_delta:"} + number(value.db());
        else
            return std::string{"boolean:"} + (value ? "true" : "false");
    }, input);
}

struct LinearExpression final {
    std::map<std::string, int, std::less<>> terms;
};

void add_scaled(LinearExpression& target, const LinearExpression& source, int scale) {
    for (const auto& [term, coefficient] : source.terms) {
        target.terms[term] += coefficient * scale;
        if (target.terms[term] == 0) target.terms.erase(term);
    }
}

class Normalizer final {
public:
    explicit Normalizer(const ir::Program& program) : program_(program) {}

    std::optional<std::string> normalize(ir::ValueId id) {
        if (id.value >= program_.values.size() || !active_.insert(id.value).second)
            return std::nullopt;
        auto result = normalize_value(id);
        active_.erase(id.value);
        return result;
    }

private:
    std::string leaf(ir::ValueId id) const {
        const auto& value = program_.values[id.value];
        return std::string{ir::type_name(value.type)} + "{" +
               std::visit([&](const auto& definition) -> std::string {
                   using T = std::decay_t<decltype(definition)>;
                   if constexpr (std::is_same_v<T, ir::Constant>) return constant(definition.value);
                   else if constexpr (std::is_same_v<T, ir::ReferencePower>)
                       return "reference_power:" + number(definition.value.dbm());
                   else if constexpr (std::is_same_v<T, ir::SweepFrequency>) return "sweep:frequency";
                   else if constexpr (std::is_same_v<T, ir::SweepTemperature>) return "sweep:temperature";
                   else if constexpr (std::is_same_v<T, ir::MeasurePower>) return "measure:power";
                   else if constexpr (std::is_same_v<T, ir::MeasureEvm>) return "measure:evm";
                   else if constexpr (std::is_same_v<T, ir::MeasureFrequencyError>) return "measure:frequency_error";
                   else return "value:" + std::to_string(id.value);
               }, value.definition) + "}";
    }

    std::optional<LinearExpression> linear(ir::ValueId id) {
        if (id.value >= program_.values.size()) return std::nullopt;
        const auto& definition = program_.values[id.value].definition;
        if (const auto* alias = std::get_if<ir::Alias>(&definition)) return linear(alias->value);
        if (const auto* negate = std::get_if<ir::NegatePowerDelta>(&definition)) {
            auto operand = linear(negate->operand);
            if (!operand) return std::nullopt;
            LinearExpression result;
            add_scaled(result, *operand, -1);
            return result;
        }
        if (const auto* negate = std::get_if<ir::NegateDelta>(&definition)) {
            auto operand = linear(negate->operand);
            if (!operand) return std::nullopt;
            LinearExpression result;
            add_scaled(result, *operand, -1);
            return result;
        }
        if (const auto* add = std::get_if<ir::AddPowerDelta>(&definition)) {
            auto left = linear(add->left); auto right = linear(add->right);
            if (!left || !right) return std::nullopt;
            add_scaled(*left, *right, 1); return left;
        }
        if (const auto* add = std::get_if<ir::AddDelta>(&definition)) {
            auto left = linear(add->left); auto right = linear(add->right);
            if (!left || !right) return std::nullopt;
            add_scaled(*left, *right, 1); return left;
        }
        if (const auto* subtract = std::get_if<ir::SubtractDelta>(&definition)) {
            auto left = linear(subtract->left); auto right = linear(subtract->right);
            if (!left || !right) return std::nullopt;
            add_scaled(*left, *right, -1); return left;
        }
        if (const auto* difference = std::get_if<ir::PowerDifference>(&definition)) {
            auto left = linear(difference->left); auto right = linear(difference->right);
            if (!left || !right) return std::nullopt;
            add_scaled(*left, *right, -1); return left;
        }
        if (const auto* absolute = std::get_if<ir::AbsoluteValue>(&definition)) {
            auto operand = normalize(absolute->operand);
            if (!operand) return std::nullopt;
            LinearExpression result;
            result.terms.emplace("absolute(" + *operand + ')', 1);
            return result;
        }
        LinearExpression result;
        result.terms.emplace(leaf(id), 1);
        return result;
    }

    std::optional<std::string> normalized_linear(ir::ValueId id) {
        auto expression = linear(id);
        if (!expression) return std::nullopt;
        std::string result{"linear("};
        bool first = true;
        for (const auto& [term, coefficient] : expression->terms) {
            if (!first) result += ',';
            first = false;
            result += std::to_string(coefficient) + "*" + term;
        }
        return result + ')';
    }

    std::optional<std::string> normalize_value(ir::ValueId id) {
        const auto& value = program_.values[id.value];
        const auto& definition = value.definition;
        if (std::holds_alternative<ir::Alias>(definition))
            return normalize(std::get<ir::Alias>(definition).value);
        if (value.type == ir::ValueType::PowerDelta ||
            value.type == ir::ValueType::FrequencyDelta ||
            value.type == ir::ValueType::TemperatureDelta ||
            value.type == ir::ValueType::Ratio) return normalized_linear(id);
        if (const auto* apply = std::get_if<ir::ApplyPowerDelta>(&definition)) {
            auto left = normalize(apply->power); auto right = normalize(apply->delta);
            if (!left || !right) return std::nullopt;
            return "apply_power_delta(" + *left + ',' + *right + ')';
        }
        if (const auto* absolute = std::get_if<ir::AbsoluteValue>(&definition)) {
            auto operand = normalize(absolute->operand);
            if (!operand) return std::nullopt;
            return "absolute(" + *operand + ')';
        }
        if (const auto* comparison = std::get_if<ir::Compare>(&definition)) {
            auto left = normalize(comparison->left); auto right = normalize(comparison->right);
            if (!left || !right) return std::nullopt;
            return "compare:" + operator_name(comparison->op) + '(' + *left + ',' + *right + ')';
        }
        return leaf(id);
    }

    const ir::Program& program_;
    std::set<std::size_t> active_;
};

std::optional<ir::SweepDimension> index_dimension(const ir::Program& program,
                                                  ir::ValueId value) {
    const auto found = std::ranges::find(program.sweeps, value, &ir::TypedSweep::value);
    if (found == program.sweeps.end()) return std::nullopt;
    return found->dimension;
}

std::string sweep_text(const SweepFact& value) {
    return dimension_name(value.dimension) + "[" + std::to_string(value.order) + "]";
}

RequirementOutcome outcome(const RequirementClause& requirement,
                           RequirementOutcomeStatus status, std::string code,
                           std::string expected, std::string actual,
                           std::string explanation) {
    return {requirement.id, status, std::move(code), std::move(expected),
            std::move(actual), std::move(explanation)};
}

} // namespace

std::optional<std::string> normalize_expression(const ir::Program& program,
                                                ir::ValueId value) {
    return Normalizer{program}.normalize(value);
}

std::optional<ProgramFacts> project_program_facts(const ir::Program& program) {
    if (program.reference.value >= program.values.size()) return std::nullopt;
    const auto* reference = std::get_if<ir::ReferencePower>(
        &program.values[program.reference.value].definition);
    if (reference == nullptr) return std::nullopt;
    ProgramFacts result;
    result.reference_power = reference->value;
    for (std::size_t order = 0; order < program.sweeps.size(); ++order) {
        const auto& sweep = program.sweeps[order];
        result.sweeps.push_back({sweep.dimension, sweep.start, sweep.end, sweep.step,
                                 sweep.point_count, order});
    }
    for (const auto& measurement : program.measurements) {
        result.measurements.push_back({measurement.name, measurement.operation,
                                       measurement.type, measurement.legacy});
    }
    for (const auto& named : program.named_values) {
        auto expression = normalize_expression(program, named.value);
        if (!expression) return std::nullopt;
        result.derived_values.push_back({named.name, named.type, std::move(*expression)});
    }
    for (const auto& assertion : program.assertions) {
        auto left = normalize_expression(program, assertion.left);
        auto right = normalize_expression(program, assertion.right);
        if (!left || !right) return std::nullopt;
        result.assertions.push_back({assertion.ordinal, assertion.op,
                                     assertion.operand_type, std::move(*left),
                                     std::move(*right)});
    }
    auto correction = normalize_expression(program, program.calibration.correction);
    if (!correction) return std::nullopt;
    result.calibration.name = program.calibration.name;
    result.calibration.correction = std::move(*correction);
    for (const auto index : program.calibration.indexes) {
        auto dimension = index_dimension(program, index);
        if (!dimension) return std::nullopt;
        result.calibration.indexes.push_back(*dimension);
    }
    return result;
}

std::optional<ProgramFacts> compile_program_facts(std::string_view source) {
    auto validated = frontend::validate(source);
    if (!validated.ok()) return std::nullopt;
    auto lowered = ir::lower(*validated.program);
    if (!lowered.ok()) return std::nullopt;
    return project_program_facts(*lowered.program);
}

RequirementVerificationResult verify_requirements(
    const std::vector<RequirementClause>& requirements, const ProgramFacts& facts) {
    RequirementVerificationResult result{RequirementSatisfactionStatus::Satisfied, {}};
    for (const auto& requirement : requirements) {
        RequirementOutcome item;
        if (requirement.kind == RequirementKind::ReferencePower && requirement.reference_power) {
            const bool match = facts.reference_power == *requirement.reference_power;
            item = outcome(requirement, match ? RequirementOutcomeStatus::Satisfied
                                              : RequirementOutcomeStatus::Violated,
                           match ? "reference_power_match" : "reference_power_mismatch",
                           number(requirement.reference_power->dbm()) + " dBm",
                           number(facts.reference_power.dbm()) + " dBm",
                           match ? "reference power matches" : "reference power differs");
        } else if (requirement.kind == RequirementKind::Sweep && requirement.sweep) {
            const auto& expected = *requirement.sweep;
            const auto found = std::ranges::find(facts.sweeps, expected.dimension,
                                                 &SweepFact::dimension);
            bool match = found != facts.sweeps.end();
            if (match) {
                match = found->start == expected.start && found->end == expected.end;
                if (requirement.constrain_sweep_step) match = match && found->step == expected.step;
                if (requirement.constrain_sweep_order) match = match && found->order == expected.order;
            }
            item = outcome(requirement, match ? RequirementOutcomeStatus::Satisfied
                                              : RequirementOutcomeStatus::Violated,
                           match ? "sweep_match" : "sweep_mismatch", sweep_text(expected),
                           found == facts.sweeps.end() ? "missing" : sweep_text(*found),
                           match ? "sweep requirement matches" : "sweep is missing or differs");
        } else if (requirement.kind == RequirementKind::Measurement && requirement.measurement) {
            const auto& expected = *requirement.measurement;
            const auto found = std::ranges::find(facts.measurements, expected.operation,
                                                 &MeasurementFact::operation);
            bool match = found != facts.measurements.end();
            if (match && requirement.constrain_measurement_name)
                match = found->name == expected.name;
            item = outcome(requirement, match ? RequirementOutcomeStatus::Satisfied
                                              : RequirementOutcomeStatus::Violated,
                           match ? "measurement_match" : "measurement_missing_or_misnamed",
                           operation_name(expected.operation) + ":" + expected.name,
                           found == facts.measurements.end()
                               ? "missing"
                               : operation_name(found->operation) + ":" + found->name,
                           match ? "measurement requirement matches"
                                 : "measurement is missing or has the wrong required name");
        } else if (requirement.kind == RequirementKind::Assertion && requirement.assertion) {
            const auto& expected = *requirement.assertion;
            const auto found = std::ranges::find_if(facts.assertions, [&](const AssertionFact& value) {
                return value.op == expected.op && value.operand_type == expected.operand_type &&
                       value.left == expected.left && value.right == expected.right;
            });
            const bool match = found != facts.assertions.end();
            item = outcome(requirement, match ? RequirementOutcomeStatus::Satisfied
                                              : RequirementOutcomeStatus::Violated,
                           match ? "assertion_match" : "assertion_missing_or_different",
                           operator_name(expected.op) + ':' + expected.left + ':' + expected.right,
                           match ? "matched" : "missing", match ? "assertion requirement matches"
                                                                  : "assertion is missing or differs");
        } else if (requirement.kind == RequirementKind::Calibration && requirement.calibration) {
            const auto& expected = *requirement.calibration;
            const bool match = facts.calibration.correction == expected.correction &&
                               facts.calibration.indexes == expected.indexes;
            item = outcome(requirement, match ? RequirementOutcomeStatus::Satisfied
                                              : RequirementOutcomeStatus::Violated,
                           match ? "calibration_match" : "calibration_mismatch",
                           expected.correction, facts.calibration.correction,
                           match ? "calibration requirement matches"
                                 : "calibration correction or indexes differ");
        } else {
            item = outcome(requirement, RequirementOutcomeStatus::Unknown,
                           "requirement_incomplete", "typed requirement", "unavailable",
                           "the requirement does not contain the typed fields needed for verification");
        }
        if (item.status == RequirementOutcomeStatus::Unsupported)
            result.status = RequirementSatisfactionStatus::Unsupported;
        else if (item.status == RequirementOutcomeStatus::Violated &&
                 result.status != RequirementSatisfactionStatus::Unsupported)
            result.status = RequirementSatisfactionStatus::Violated;
        else if (item.status == RequirementOutcomeStatus::Unknown &&
                 result.status == RequirementSatisfactionStatus::Satisfied)
            result.status = RequirementSatisfactionStatus::Unknown;
        result.outcomes.push_back(std::move(item));
    }
    if (requirements.empty()) result.status = RequirementSatisfactionStatus::NotEvaluated;
    return result;
}

} // namespace horusrf::ai
