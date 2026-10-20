#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "horusrf/semantic/model.hpp"

namespace horusrf::semantic {

struct MeasurementDefinition {
    std::string_view name;
    MeasurementOperation operation;
    SemanticType result_type;
};

enum class SweepDimension { Frequency, Temperature };

struct SweepDefinition {
    std::string_view name;
    SweepDimension dimension;
    SemanticType endpoint_type;
    SemanticType step_type;
    bool required;
};

struct FunctionDefinition {
    std::string_view name;
    std::vector<SemanticType> argument_types;
};

inline constexpr std::size_t maximum_sweep_point_count = 1'000'000;

[[nodiscard]] const std::vector<MeasurementDefinition>& measurement_definitions();
[[nodiscard]] std::optional<MeasurementDefinition>
measurement_definition(std::string_view name);

[[nodiscard]] const std::vector<SweepDefinition>& sweep_definitions();
[[nodiscard]] std::optional<SweepDefinition> sweep_definition(std::string_view name);

[[nodiscard]] const std::vector<FunctionDefinition>& function_definitions();
[[nodiscard]] const FunctionDefinition* function_definition(std::string_view name);

} // namespace horusrf::semantic
