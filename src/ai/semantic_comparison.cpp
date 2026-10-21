#include "horusrf/ai/semantic_comparison.hpp"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "horusrf/frontend/language_services.hpp"
#include "horusrf/ir/ir.hpp"
#include "horusrf/ir/lowering.hpp"

namespace horusrf::ai {
namespace {

struct CompiledProgram final {
    std::optional<ir::Program> program;
    std::string error;
};

std::string number(double value) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(17) << value;
    return output.str();
}

std::string value_type(ir::ValueType type) {
    return std::string{ir::type_name(type)};
}

std::string comparison_operator(ir::ComparisonOperator op) {
    switch (op) {
    case ir::ComparisonOperator::Less: return "less";
    case ir::ComparisonOperator::LessEqual: return "less_equal";
    case ir::ComparisonOperator::Greater: return "greater";
    case ir::ComparisonOperator::GreaterEqual: return "greater_equal";
    case ir::ComparisonOperator::Equal: return "equal";
    case ir::ComparisonOperator::NotEqual: return "not_equal";
    }
    return "unknown";
}

std::string measurement_operation(ir::MeasurementOperation operation) {
    switch (operation) {
    case ir::MeasurementOperation::Power: return "power";
    case ir::MeasurementOperation::Evm: return "evm";
    case ir::MeasurementOperation::FrequencyError: return "frequency_error";
    }
    return "unknown";
}

std::string sweep_dimension(ir::SweepDimension dimension) {
    switch (dimension) {
    case ir::SweepDimension::Frequency: return "frequency";
    case ir::SweepDimension::Temperature: return "temperature";
    }
    return "unknown";
}

std::string constant(const ir::ConstantValue& input) {
    return std::visit(
        [](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, domain::Frequency>) {
                return std::string{"frequency:"} + number(value.hertz());
            } else if constexpr (std::is_same_v<T, domain::FrequencyDelta>) {
                return std::string{"frequency_delta:"} + number(value.hertz());
            } else if constexpr (std::is_same_v<T, domain::Ratio>) {
                return std::string{"ratio:"} + number(value.ratio());
            } else if constexpr (std::is_same_v<T, domain::Temperature>) {
                return std::string{"temperature:"} + number(value.kelvin());
            } else if constexpr (std::is_same_v<T, domain::TemperatureDelta>) {
                return std::string{"temperature_delta:"} + number(value.kelvin());
            } else if constexpr (std::is_same_v<T, domain::Power>) {
                return std::string{"power:"} + number(value.dbm());
            } else if constexpr (std::is_same_v<T, domain::PowerDelta>) {
                return std::string{"power_delta:"} + number(value.db());
            } else {
                return std::string{"boolean:"} + (value ? "true" : "false");
            }
        },
        input);
}

CompiledProgram compile(std::string_view source) {
    auto validated = frontend::validate(source);
    if (!validated.ok()) {
        const auto message = validated.diagnostics.empty()
                                 ? std::string{"frontend validation failed"}
                                 : validated.diagnostics.front().code + ": " +
                                       validated.diagnostics.front().message;
        return {std::nullopt, message};
    }
    auto lowered = ir::lower(*validated.program);
    if (!lowered.ok()) {
        const auto message = lowered.diagnostics.empty()
                                 ? std::string{"IR lowering failed"}
                                 : std::string{ir::diagnostic_code_name(
                                       lowered.diagnostics.front().code)} +
                                       ": " + lowered.diagnostics.front().message;
        return {std::nullopt, message};
    }
    return {std::move(lowered.program), {}};
}

class ExpressionNormalizer final {
public:
    explicit ExpressionNormalizer(const ir::Program& program) : program_(program) {}

    std::optional<std::string> normalize(ir::ValueId id) {
        if (id.value >= program_.values.size()) return std::nullopt;
        if (!active_.insert(id.value).second) return std::nullopt;
        const auto& value = program_.values[id.value];
        auto result = std::visit(
            [this](const auto& definition) { return normalize_definition(definition); },
            value.definition);
        active_.erase(id.value);
        if (!result) return std::nullopt;
        return value_type(value.type) + "{" + *result + "}";
    }

private:
    std::optional<std::string> unary(std::string_view name, ir::ValueId operand) {
        auto normalized = normalize(operand);
        if (!normalized) return std::nullopt;
        return std::string{name} + "(" + *normalized + ")";
    }

    std::optional<std::string> binary(std::string_view name, ir::ValueId left,
                                      ir::ValueId right) {
        auto normalized_left = normalize(left);
        auto normalized_right = normalize(right);
        if (!normalized_left || !normalized_right) return std::nullopt;
        return std::string{name} + "(" + *normalized_left + "," +
               *normalized_right + ")";
    }

    std::optional<std::string> normalize_definition(const ir::Constant& value) {
        return constant(value.value);
    }
    std::optional<std::string> normalize_definition(const ir::ReferencePower& value) {
        return "reference_power:" + number(value.value.dbm());
    }
    std::optional<std::string> normalize_definition(const ir::SweepFrequency&) {
        return "sweep:frequency";
    }
    std::optional<std::string> normalize_definition(const ir::SweepTemperature&) {
        return "sweep:temperature";
    }
    std::optional<std::string> normalize_definition(const ir::MeasurePower&) {
        return "measure:power";
    }
    std::optional<std::string> normalize_definition(const ir::MeasureEvm&) {
        return "measure:evm";
    }
    std::optional<std::string> normalize_definition(const ir::MeasureFrequencyError&) {
        return "measure:frequency_error";
    }
    std::optional<std::string> normalize_definition(const ir::NegateDelta& value) {
        return unary("negate", value.operand);
    }
    std::optional<std::string> normalize_definition(const ir::AddDelta& value) {
        return binary("add", value.left, value.right);
    }
    std::optional<std::string> normalize_definition(const ir::SubtractDelta& value) {
        return binary("subtract", value.left, value.right);
    }
    std::optional<std::string> normalize_definition(const ir::Alias& value) {
        return normalize(value.value);
    }
    std::optional<std::string> normalize_definition(const ir::NegatePowerDelta& value) {
        return unary("negate", value.operand);
    }
    std::optional<std::string> normalize_definition(const ir::AddPowerDelta& value) {
        return binary("add", value.left, value.right);
    }
    std::optional<std::string> normalize_definition(const ir::ApplyPowerDelta& value) {
        return binary("apply_power_delta", value.power, value.delta);
    }
    std::optional<std::string> normalize_definition(const ir::PowerDifference& value) {
        return binary("power_difference", value.left, value.right);
    }
    std::optional<std::string> normalize_definition(const ir::AbsoluteValue& value) {
        return unary("absolute", value.operand);
    }
    std::optional<std::string> normalize_definition(const ir::Compare& value) {
        auto operands = binary(comparison_operator(value.op), value.left, value.right);
        if (!operands) return std::nullopt;
        return "compare:" + value_type(value.operand_type) + ":" + *operands;
    }

    const ir::Program& program_;
    std::set<std::size_t> active_;
};

void add_difference(SemanticComparisonResult& result, std::string path,
                    std::string expected, std::string actual) {
    result.differences.push_back(
        {std::move(path), std::move(expected), std::move(actual)});
}

template <typename T, typename Render>
void compare_scalar(SemanticComparisonResult& result, std::string path,
                    const T& expected, const T& actual, Render render) {
    if (expected != actual) {
        add_difference(result, std::move(path), render(expected), render(actual));
    }
}

std::string endpoint(const ir::SweepEndpoint& value) {
    return std::visit(
        [](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, domain::Frequency>) {
                return number(item.hertz()) + " Hz";
            } else {
                return number(item.kelvin()) + " K";
            }
        },
        value);
}

std::string step(const ir::SweepStep& value) {
    return std::visit(
        [](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, domain::Frequency>) {
                return number(item.hertz()) + " Hz";
            } else {
                return number(item.kelvin()) + " deltaK";
            }
        },
        value);
}

template <typename Items>
std::string joined(const Items& items) {
    std::string output{"["};
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (index != 0) output += ", ";
        output += items[index];
    }
    output += "]";
    return output;
}

std::optional<std::vector<std::string>> normalized_named_values(
    const ir::Program& program, ExpressionNormalizer& normalizer) {
    std::vector<std::string> values;
    values.reserve(program.named_values.size());
    for (const auto& named : program.named_values) {
        auto expression = normalizer.normalize(named.value);
        if (!expression) return std::nullopt;
        values.push_back(value_type(named.type) + ":" + *expression);
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::optional<std::vector<std::string>> normalized_assertions(
    const ir::Program& program, ExpressionNormalizer& normalizer) {
    std::vector<std::string> values;
    values.reserve(program.assertions.size());
    for (const auto& assertion : program.assertions) {
        auto expression = normalizer.normalize(assertion.value);
        if (!expression) return std::nullopt;
        values.push_back(*expression);
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::string> normalized_measurements(const ir::Program& program) {
    std::vector<std::string> values;
    values.reserve(program.measurements.size());
    for (const auto& measurement : program.measurements) {
        values.push_back(measurement_operation(measurement.operation) + ":" +
                         value_type(measurement.type));
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::optional<std::vector<std::string>> calibration_indexes(
    const ir::Program& program) {
    std::vector<std::string> values;
    values.reserve(program.calibration.indexes.size());
    for (std::size_t index = 0; index < program.calibration.indexes.size(); ++index) {
        const auto value = program.calibration.indexes[index];
        const auto sweep = std::find_if(
            program.sweeps.begin(), program.sweeps.end(),
            [value](const ir::TypedSweep& candidate) { return candidate.value == value; });
        if (sweep == program.sweeps.end() ||
            index >= program.calibration.index_types.size()) return std::nullopt;
        values.push_back(sweep_dimension(sweep->dimension) + ":" +
                         value_type(program.calibration.index_types[index]));
    }
    return values;
}

SemanticComparisonResult compare_programs(const ir::Program& expected,
                                          const ir::Program& actual) {
    SemanticComparisonResult result{SemanticComparisonStatus::Match, {}};

    ExpressionNormalizer expected_normalizer{expected};
    ExpressionNormalizer actual_normalizer{actual};
    const auto expected_reference = expected_normalizer.normalize(expected.reference);
    const auto actual_reference = actual_normalizer.normalize(actual.reference);
    if (!expected_reference || !actual_reference) {
        return {SemanticComparisonStatus::NotEvaluated,
                {{"reference", expected_reference.value_or("unavailable"),
                  actual_reference.value_or("unavailable")}}};
    }
    compare_scalar(result, "reference", *expected_reference, *actual_reference,
                   [](const std::string& value) { return value; });

    if (expected.sweeps.size() != actual.sweeps.size()) {
        add_difference(result, "sweeps.count", std::to_string(expected.sweeps.size()),
                       std::to_string(actual.sweeps.size()));
    }
    const auto sweep_count = std::min(expected.sweeps.size(), actual.sweeps.size());
    for (std::size_t index = 0; index < sweep_count; ++index) {
        const auto path = "sweeps[" + std::to_string(index) + "]";
        const auto& expected_sweep = expected.sweeps[index];
        const auto& actual_sweep = actual.sweeps[index];
        compare_scalar(result, path + ".dimension", expected_sweep.dimension,
                       actual_sweep.dimension, sweep_dimension);
        compare_scalar(result, path + ".start", expected_sweep.start,
                       actual_sweep.start, endpoint);
        compare_scalar(result, path + ".end", expected_sweep.end,
                       actual_sweep.end, endpoint);
        compare_scalar(result, path + ".step", expected_sweep.step,
                       actual_sweep.step, step);
        compare_scalar(result, path + ".point_count", expected_sweep.point_count,
                       actual_sweep.point_count,
                       [](std::size_t value) { return std::to_string(value); });
    }

    const auto expected_measurements = normalized_measurements(expected);
    const auto actual_measurements = normalized_measurements(actual);
    compare_scalar(result, "measurements", expected_measurements, actual_measurements,
                   joined<std::vector<std::string>>);

    const auto expected_named = normalized_named_values(expected, expected_normalizer);
    const auto actual_named = normalized_named_values(actual, actual_normalizer);
    const auto expected_assertions = normalized_assertions(expected, expected_normalizer);
    const auto actual_assertions = normalized_assertions(actual, actual_normalizer);
    const auto expected_indexes = calibration_indexes(expected);
    const auto actual_indexes = calibration_indexes(actual);
    const auto expected_correction =
        expected_normalizer.normalize(expected.calibration.correction);
    const auto actual_correction = actual_normalizer.normalize(actual.calibration.correction);
    if (!expected_named || !actual_named || !expected_assertions || !actual_assertions ||
        !expected_indexes || !actual_indexes || !expected_correction ||
        !actual_correction) {
        return {SemanticComparisonStatus::NotEvaluated,
                {{"expressions", "normalizable IR", "unsupported or malformed IR"}}};
    }

    compare_scalar(result, "derived_values", *expected_named, *actual_named,
                   joined<std::vector<std::string>>);
    compare_scalar(result, "assertions", *expected_assertions, *actual_assertions,
                   joined<std::vector<std::string>>);
    compare_scalar(result, "calibration.indexes", *expected_indexes, *actual_indexes,
                   joined<std::vector<std::string>>);
    compare_scalar(result, "calibration.correction_type",
                   expected.calibration.correction_type,
                   actual.calibration.correction_type, value_type);
    compare_scalar(result, "calibration.correction", *expected_correction,
                   *actual_correction,
                   [](const std::string& value) { return value; });

    if (!result.differences.empty()) result.status = SemanticComparisonStatus::Mismatch;
    return result;
}

} // namespace

SemanticComparisonResult compare_script_semantics(std::string_view expected_source,
                                                  std::string_view actual_source) {
    const auto expected = compile(expected_source);
    if (!expected.program) {
        return {SemanticComparisonStatus::NotEvaluated,
                {{"expected.compile", "valid HorusRF script", expected.error}}};
    }
    const auto actual = compile(actual_source);
    if (!actual.program) {
        return {SemanticComparisonStatus::NotEvaluated,
                {{"actual.compile", "valid HorusRF script", actual.error}}};
    }
    return compare_programs(*expected.program, *actual.program);
}

} // namespace horusrf::ai
