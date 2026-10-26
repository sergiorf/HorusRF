#include "horusrf/ai/requirement_verification.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <locale>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <type_traits>
#include <utility>
#include <variant>

#include "horusrf/frontend/language_services.hpp"
#include "horusrf/domain/units.hpp"
#include "horusrf/ir/lowering.hpp"

namespace horusrf::ai {
namespace {

std::string number(double value) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(17) << value;
    return output.str();
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

struct NormalizedInstruction final {
    std::string text;
    std::vector<std::size_t> original_offsets;
};

NormalizedInstruction normalize_punctuation(std::string_view input) {
    NormalizedInstruction result;
    for (std::size_t index = 0; index < input.size();) {
        const bool dash = index + 2 < input.size() &&
            static_cast<unsigned char>(input[index]) == 0xe2 &&
            static_cast<unsigned char>(input[index + 1]) == 0x80 &&
            (static_cast<unsigned char>(input[index + 2]) == 0x92 ||
             static_cast<unsigned char>(input[index + 2]) == 0x93 ||
             static_cast<unsigned char>(input[index + 2]) == 0x94);
        result.original_offsets.push_back(index);
        if (dash) {
            result.text.push_back('-');
            index += 3;
        } else {
            result.text.push_back(input[index++]);
        }
    }
    result.original_offsets.push_back(input.size());
    return result;
}

std::pair<std::size_t, std::size_t> original_span(
    const NormalizedInstruction& normalized, std::size_t begin, std::size_t end) {
    return {normalized.original_offsets.at(begin), normalized.original_offsets.at(end)};
}

constexpr std::string_view decimal_pattern =
    R"([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+))";

std::optional<domain::CanonicalQuantity> quantity(std::string_view value,
                                                   std::string_view unit) {
    if (lowercase(std::string{unit}) == "mdbm") {
        const auto converted = domain::convert_quantity(value, "dBm");
        if (!converted.value) return std::nullopt;
        return domain::CanonicalQuantity{domain::Power::from_dbm(
            std::get<domain::Power>(*converted.value).dbm() / 1000.0)};
    }
    std::string canonical_unit{unit};
    const std::string lowered = lowercase(canonical_unit);
    if (lowered == "hz") canonical_unit = "Hz";
    else if (lowered == "khz") canonical_unit = "kHz";
    else if (lowered == "mhz") canonical_unit = "MHz";
    else if (lowered == "ghz") canonical_unit = "GHz";
    else if (lowered == "dbm") canonical_unit = "dBm";
    else if (lowered == "db") canonical_unit = "dB";
    else if (lowered == "degc") canonical_unit = "degC";
    else if (lowered == "deltac") canonical_unit = "deltaC";
    else if (lowered == "deltak") canonical_unit = "deltaK";
    const auto converted = domain::convert_quantity(value, canonical_unit);
    return converted.value;
}

RequirementClause extracted_clause(std::string id, RequirementKind kind,
                                    std::string_view original,
                                    const NormalizedInstruction& normalized,
                                    std::size_t begin, std::size_t end) {
    const auto [source_begin, source_end] = original_span(normalized, begin, end);
    RequirementClause clause;
    clause.id = std::move(id);
    clause.kind = kind;
    clause.provenance = "interactive-high-confidence";
    clause.source_begin = source_begin;
    clause.source_end = source_end;
    clause.source_text = std::string{original.substr(source_begin,
                                                     source_end - source_begin)};
    return clause;
}

std::string threshold_identity(ir::ValueType type,
                               const domain::CanonicalQuantity& value) {
    if (type == ir::ValueType::PowerDelta) {
        return "linear(1*PowerDelta{power_delta:" +
               number(std::get<domain::PowerDelta>(value).db()) + "})";
    }
    if (type == ir::ValueType::Ratio) {
        return "linear(1*Ratio{ratio:" +
               number(std::get<domain::Ratio>(value).ratio()) + "})";
    }
    const auto frequency = std::get<domain::Frequency>(value);
    return "linear(1*FrequencyDelta{frequency_delta:" +
           number(frequency.hertz()) + "})";
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

std::string calibration_correction_identity(
    CalibrationCorrectionRelation relation, domain::Power reference_power) {
    const std::string measured{"Power{measure:power}"};
    const std::string reference{"Power{reference_power:" +
                                number(reference_power.dbm()) + "}"};
    const int measured_coefficient =
        relation == CalibrationCorrectionRelation::ReferenceMinusMeasuredPower
            ? -1
            : 1;
    const int reference_coefficient = -measured_coefficient;
    return "linear(" + std::to_string(measured_coefficient) + "*" + measured +
           "," + std::to_string(reference_coefficient) + "*" + reference + ")";
}

RequirementOutcome outcome(const RequirementClause& requirement,
                           RequirementOutcomeStatus status, std::string code,
                           std::string expected, std::string actual,
                           std::string explanation) {
    return {requirement.id, status, std::move(code), std::move(expected),
            std::move(actual), std::move(explanation), requirement.confidence,
            requirement.source_begin, requirement.source_end,
            requirement.source_text};
}

} // namespace

std::vector<RequirementClause> extract_requirements(std::string_view instruction) {
    const auto normalized = normalize_punctuation(instruction);
    const std::string lowered = lowercase(normalized.text);
    std::vector<RequirementClause> result;
    std::size_t identifier{};
    auto next_id = [&](std::string_view prefix) {
        return std::string{prefix} + '-' + std::to_string(++identifier);
    };

    struct PositionedSweep final {
        std::size_t position{};
        RequirementClause clause;
    };
    std::vector<PositionedSweep> sweeps;
    const auto extract_ranges = [&](const std::regex& pattern,
                                    ir::SweepDimension dimension) {
        for (std::sregex_iterator match{normalized.text.begin(), normalized.text.end(),
                                        pattern}, end;
             match != end; ++match) {
            const std::string start_unit = (*match)[2].matched
                                               ? (*match)[2].str()
                                               : (*match)[4].str();
            auto start = quantity((*match)[1].str(), start_unit);
            auto finish = quantity((*match)[3].str(), (*match)[4].str());
            std::string step_unit = (*match)[6].str();
            if (lowercase(step_unit).starts_with("degree")) {
                step_unit = lowercase((*match)[4].str()) == "degc" ? "deltaC"
                                                                    : "deltaK";
            }
            auto step = quantity((*match)[5].str(), step_unit);
            if (!start || !finish || !step) continue;
            auto clause = extracted_clause(next_id("sweep"), RequirementKind::Sweep,
                                           instruction, normalized,
                                           static_cast<std::size_t>(match->position()),
                                           static_cast<std::size_t>(match->position() +
                                                                    match->length()));
            std::optional<SweepFact> fact;
            if (dimension == ir::SweepDimension::Frequency &&
                std::holds_alternative<domain::Frequency>(*start) &&
                std::holds_alternative<domain::Frequency>(*finish) &&
                std::holds_alternative<domain::Frequency>(*step)) {
                fact.emplace(SweepFact{
                    dimension, std::get<domain::Frequency>(*start),
                    std::get<domain::Frequency>(*finish),
                    std::get<domain::Frequency>(*step), 0, 0});
            } else if (dimension == ir::SweepDimension::Temperature &&
                       std::holds_alternative<domain::Temperature>(*start) &&
                       std::holds_alternative<domain::Temperature>(*finish) &&
                       std::holds_alternative<domain::TemperatureDelta>(*step)) {
                fact.emplace(SweepFact{
                    dimension, std::get<domain::Temperature>(*start),
                    std::get<domain::Temperature>(*finish),
                    std::get<domain::TemperatureDelta>(*step), 0, 0});
            } else {
                continue;
            }
            clause.sweep = std::move(*fact);
            clause.constrain_sweep_order = false;
            sweeps.push_back({static_cast<std::size_t>(match->position()),
                              std::move(clause)});
        }
    };

    const std::string number_pattern{decimal_pattern};
    const std::regex frequency_range{
        "(" + number_pattern + ")\\s*(Hz|kHz|MHz|GHz)?\\s*"
        "(?:\\.\\.|to|through|and)\\s*(" + number_pattern +
        ")\\s*(Hz|kHz|MHz|GHz)\\s*(?:(?:in|by|every|at)\\s*)?(" +
        number_pattern + ")\\s*(Hz|kHz|MHz|GHz)(?:\\s*(?:steps?|spacing|increments?))?",
        std::regex_constants::icase};
    const std::regex temperature_range{
        "(" + number_pattern + ")\\s*(K|degC)?\\s*"
        "(?:\\.\\.|to|through|and)\\s*(" + number_pattern +
        ")\\s*(K|degC)\\s*(?:(?:in|by|every|at)\\s*)?(" +
        number_pattern + ")\\s*(deltaK|deltaC|degrees?)(?:\\s*(?:steps?|spacing|increments?))?",
        std::regex_constants::icase};
    extract_ranges(frequency_range, ir::SweepDimension::Frequency);
    extract_ranges(temperature_range, ir::SweepDimension::Temperature);

    const bool has_frequency_range = std::ranges::any_of(sweeps, [](const auto& item) {
        return item.clause.sweep->dimension == ir::SweepDimension::Frequency;
    });
    if (!has_frequency_range) {
        const std::regex fixed_frequency{
            "(?:fixed\\s+|once\\s+)?at\\s+(" + number_pattern +
            ")\\s*(Hz|kHz|MHz|GHz)\\b",
            std::regex_constants::icase};
        std::smatch match;
        if (std::regex_search(normalized.text, match, fixed_frequency)) {
            if (auto value = quantity(match[1].str(), match[2].str()); value &&
                std::holds_alternative<domain::Frequency>(*value)) {
                auto clause = extracted_clause(
                    next_id("fixed-frequency"), RequirementKind::Sweep, instruction,
                    normalized, static_cast<std::size_t>(match.position()),
                    static_cast<std::size_t>(match.position() + match.length()));
                const auto frequency = std::get<domain::Frequency>(*value);
                clause.sweep = SweepFact{ir::SweepDimension::Frequency, frequency,
                                         frequency, frequency, 1, 0};
                clause.constrain_sweep_step = false;
                clause.constrain_sweep_order = false;
                sweeps.push_back({static_cast<std::size_t>(match.position()),
                                  std::move(clause)});
            }
        }
    }
    if (!has_frequency_range) {
        const std::regex frequency_step{
            "frequency\\s+step(?:\\s+to)?\\s+(" + number_pattern +
            ")\\s*(Hz|kHz|MHz|GHz)\\b",
            std::regex_constants::icase};
        std::smatch match;
        if (std::regex_search(normalized.text, match, frequency_step)) {
            if (auto value = quantity(match[1].str(), match[2].str()); value &&
                std::holds_alternative<domain::Frequency>(*value)) {
                auto clause = extracted_clause(
                    next_id("frequency-step"), RequirementKind::Sweep,
                    instruction, normalized,
                    static_cast<std::size_t>(match.position()),
                    static_cast<std::size_t>(match.position() + match.length()));
                const auto step = std::get<domain::Frequency>(*value);
                const auto placeholder = domain::Frequency::from_hertz(0.0);
                clause.sweep = SweepFact{ir::SweepDimension::Frequency,
                                         placeholder, placeholder, step, 0, 0};
                clause.constrain_sweep_bounds = false;
                clause.constrain_sweep_order = false;
                sweeps.push_back({static_cast<std::size_t>(match.position()),
                                  std::move(clause)});
            }
        }
    }
    std::ranges::sort(sweeps, {}, &PositionedSweep::position);
    const bool explicit_order = lowered.find("then sweep") != std::string::npos ||
                                lowered.find("outer sweep") != std::string::npos ||
                                lowered.find("sweep order") != std::string::npos ||
                                lowered.find("first") != std::string::npos;
    for (std::size_t order = 0; order < sweeps.size(); ++order) {
        sweeps[order].clause.sweep->order = order;
        sweeps[order].clause.constrain_sweep_order = explicit_order;
        result.push_back(std::move(sweeps[order].clause));
    }

    const std::regex absolute_power{"(" + number_pattern +
                                        ")\\s*(mdBm|dBm)\\b",
                                    std::regex_constants::icase};
    std::vector<std::smatch> powers;
    for (std::sregex_iterator match{normalized.text.begin(), normalized.text.end(),
                                    absolute_power}, end;
         match != end; ++match) {
        powers.push_back(*match);
    }
    bool equivalent_repeated_power = powers.size() > 1;
    std::optional<domain::Power> repeated_power;
    for (const auto& match : powers) {
        const auto value = quantity(match[1].str(), match[2].str());
        if (!value || !std::holds_alternative<domain::Power>(*value)) {
            equivalent_repeated_power = false;
            break;
        }
        const auto power = std::get<domain::Power>(*value);
        if (repeated_power && power != *repeated_power) equivalent_repeated_power = false;
        repeated_power = power;
    }
    if (powers.size() == 1 || equivalent_repeated_power) {
        const auto& match = powers.front();
        if (auto value = quantity(match[1].str(), match[2].str()); value &&
            std::holds_alternative<domain::Power>(*value)) {
            auto clause = extracted_clause(
                next_id("reference-power"), RequirementKind::ReferencePower,
                instruction, normalized, static_cast<std::size_t>(match.position()),
                static_cast<std::size_t>(match.position() + match.length()));
            clause.reference_power = std::get<domain::Power>(*value);
            result.push_back(std::move(clause));
        }
    } else if (powers.size() > 1) {
        auto clause = extracted_clause(next_id("ambiguous-power"),
                                       RequirementKind::ReferencePower, instruction,
                                       normalized, 0, normalized.text.size());
        clause.extraction_code = "ambiguous_absolute_power";
        clause.confidence = RequirementConfidence::Ambiguous;
        clause.extraction_explanation =
            "multiple absolute power values were present and their roles are ambiguous";
        result.push_back(std::move(clause));
    }

    const bool measurement_context =
        lowered.find("measure") != std::string::npos ||
        lowered.find("record") != std::string::npos ||
        lowered.find("collect") != std::string::npos ||
        lowered.find("reading") != std::string::npos ||
        lowered.find("output power") != std::string::npos;
    const auto add_measurement = [&](ir::MeasurementOperation operation,
                                     std::string_view operation_text,
                                     ir::ValueType type) {
        auto position = lowered.find(operation_text);
        auto operation_extent = operation_text.size();
        if (operation == ir::MeasurementOperation::Power &&
            position == std::string::npos) {
            const std::regex output_measurement{
                R"((?:record|measure|collect)\s+(?:the\s+)?output\b)",
                std::regex_constants::icase};
            std::smatch output_match;
            if (std::regex_search(normalized.text, output_match,
                                  output_measurement)) {
                position = static_cast<std::size_t>(output_match.position());
                operation_extent = static_cast<std::size_t>(output_match.length());
            }
        }
        if (!measurement_context || position == std::string::npos) return;
        auto clause = extracted_clause(next_id("measurement"),
                                       RequirementKind::Measurement, instruction,
                                       normalized, position,
                                       position + operation_extent);
        MeasurementFact fact{"", operation, type, false};
        const std::string operation_pattern = operation == ir::MeasurementOperation::Power
            ? R"((?:power(?:\s+measurement|\s+reading)?|measured\s+result))"
            : operation == ir::MeasurementOperation::Evm
                ? R"(EVM)"
                : R"((?:carrier\s+(?:frequency\s+)?error|frequency\s+error))";
        const std::regex named_after{operation_pattern +
            R"(\s+(?:as|named)\s+([A-Za-z_][A-Za-z0-9_]*))",
            std::regex_constants::icase};
        const std::regex named_result{
            R"(name\s+(?:the\s+)?(?:measured\s+result|power\s+reading)\s+([A-Za-z_][A-Za-z0-9_]*))",
            std::regex_constants::icase};
        const std::regex named_power_output{
            R"((?:record|measure|collect)\s+(?:the\s+)?output\s+as\s+([A-Za-z_][A-Za-z0-9_]*))",
            std::regex_constants::icase};
        std::smatch name_match;
        const bool found_name =
            std::regex_search(normalized.text, name_match, named_after) ||
            (operation == ir::MeasurementOperation::Power &&
             (std::regex_search(normalized.text, name_match, named_result) ||
              std::regex_search(normalized.text, name_match,
                                named_power_output)));
        const std::set<std::string, std::less<>> output_format_words{
            "a", "an", "csv", "file", "json", "report", "table", "text"};
        if (found_name &&
            !output_format_words.contains(lowercase(name_match[1].str()))) {
            fact.name = name_match[1].str();
            clause.constrain_measurement_name = true;
            const auto span = original_span(normalized,
                static_cast<std::size_t>(name_match.position()),
                static_cast<std::size_t>(name_match.position() + name_match.length()));
            clause.source_begin = span.first;
            clause.source_end = span.second;
            clause.source_text = std::string{instruction.substr(span.first,
                                                                 span.second - span.first)};
        }
        clause.measurement = std::move(fact);
        result.push_back(std::move(clause));
    };
    add_measurement(ir::MeasurementOperation::Power, "power", ir::ValueType::Power);
    add_measurement(ir::MeasurementOperation::Evm, "evm", ir::ValueType::Ratio);
    if (lowered.find("frequency error") != std::string::npos) {
        add_measurement(ir::MeasurementOperation::FrequencyError, "frequency error",
                        ir::ValueType::FrequencyDelta);
    } else {
        add_measurement(ir::MeasurementOperation::FrequencyError, "carrier error",
                        ir::ValueType::FrequencyDelta);
    }

    std::vector<ir::SweepDimension> calibration_indexes;
    std::size_t calibration_begin{};
    std::size_t calibration_end{};
    const std::regex calibration_over{
        R"((calibrate|calibration|correction(?:\s+table)?|surface|table)[^.]{0,100}(?:over|indexed\s+by)\s+(frequency\s*(?:,|and)\s*temperature|temperature\s*(?:,|and)\s*frequency|both\s+dimensions|frequency|temperature))",
        std::regex_constants::icase};
    std::smatch calibration_match;
    if (std::regex_search(normalized.text, calibration_match, calibration_over)) {
        const std::string dimensions = lowercase(calibration_match[2].str());
        calibration_begin = static_cast<std::size_t>(calibration_match.position());
        calibration_end = calibration_begin +
                          static_cast<std::size_t>(calibration_match.length());
        if (dimensions == "both dimensions") {
            for (const auto& clause : result) {
                if (clause.sweep) calibration_indexes.push_back(clause.sweep->dimension);
            }
        } else {
            const auto frequency = dimensions.find("frequency");
            const auto temperature = dimensions.find("temperature");
            if (frequency != std::string::npos &&
                (temperature == std::string::npos || frequency < temperature)) {
                calibration_indexes.push_back(ir::SweepDimension::Frequency);
            }
            if (temperature != std::string::npos) {
                calibration_indexes.push_back(ir::SweepDimension::Temperature);
            }
            if (frequency != std::string::npos && temperature != std::string::npos &&
                temperature < frequency) {
                calibration_indexes.push_back(ir::SweepDimension::Frequency);
            }
        }
    } else {
        const std::regex frequency_indexed{R"(frequency-indexed)",
                                           std::regex_constants::icase};
        if (std::regex_search(normalized.text, calibration_match,
                              frequency_indexed)) {
            calibration_begin = static_cast<std::size_t>(calibration_match.position());
            calibration_end = calibration_begin +
                              static_cast<std::size_t>(calibration_match.length());
            calibration_indexes.push_back(ir::SweepDimension::Frequency);
        }
    }
    std::optional<CalibrationCorrectionRelation> calibration_relation;
    std::size_t correction_begin{};
    std::size_t correction_end{};
    const std::regex calibration_request{
        R"(\b(calibrate|calibration|correction)\b)",
        std::regex_constants::icase};
    std::smatch calibration_request_match;
    const bool calibration_requested =
        std::regex_search(normalized.text, calibration_request_match,
                          calibration_request);

    const std::regex deviation_definition{
        R"((?:define\s+)?([A-Za-z_][A-Za-z0-9_]*)\s+as\s+([A-Za-z_][A-Za-z0-9_]*)\s+minus\s+(?:the\s+)?(?:nominal|reference(?:\s+power)?))",
        std::regex_constants::icase};
    const std::regex cancel_deviation{
        R"((?:make\s+)?(?:the\s+)?correction\s+(?:cancel|negate)s?\s+(?:that\s+|the\s+)?([A-Za-z_][A-Za-z0-9_]*))",
        std::regex_constants::icase};
    std::smatch definition_match;
    std::smatch cancellation_match;
    const auto is_power_measurement_term = [&](std::string value) {
        value = lowercase(std::move(value));
        if (value == "measured" || value == "measurement" ||
            value == "output" || value == "observed" || value == "reading") {
            return true;
        }
        return std::ranges::any_of(result, [&](const RequirementClause& clause) {
            return clause.measurement &&
                   clause.measurement->operation ==
                       ir::MeasurementOperation::Power &&
                   clause.constrain_measurement_name &&
                   lowercase(clause.measurement->name) == value;
        });
    };
    if (std::regex_search(normalized.text, definition_match,
                          deviation_definition) &&
        std::regex_search(normalized.text, cancellation_match,
                          cancel_deviation) &&
        is_power_measurement_term(definition_match[2].str()) &&
        lowercase(definition_match[1].str()) ==
            lowercase(cancellation_match[1].str())) {
        calibration_relation =
            CalibrationCorrectionRelation::ReferenceMinusMeasuredPower;
        correction_begin = std::min(
            static_cast<std::size_t>(definition_match.position()),
            static_cast<std::size_t>(cancellation_match.position()));
        correction_end = std::max(
            static_cast<std::size_t>(definition_match.position() +
                                     definition_match.length()),
            static_cast<std::size_t>(cancellation_match.position() +
                                     cancellation_match.length()));
    } else {
        const std::regex reference_minus_measured{
            R"((?:set\s+(?:the\s+)?)?(?:calibration\s+)?correction(?:\s+(?:to|is|as|equals?))?\s+(?:the\s+)?(?:nominal|reference(?:\s+power)?)\s+minus\s+(?:the\s+)?(?:measured(?:\s+power)?|measurement|output|observed|reading))",
            std::regex_constants::icase};
        const std::regex measured_minus_reference{
            R"((?:set\s+(?:the\s+)?)?(?:calibration\s+)?correction(?:\s+(?:to|is|as|equals?))?\s+(?:the\s+)?(?:measured(?:\s+power)?|measurement|output|observed|reading)\s+minus\s+(?:the\s+)?(?:nominal|reference(?:\s+power)?))",
            std::regex_constants::icase};
        std::smatch direct_match;
        if (std::regex_search(normalized.text, direct_match,
                              reference_minus_measured)) {
            calibration_relation =
                CalibrationCorrectionRelation::ReferenceMinusMeasuredPower;
        } else if (std::regex_search(normalized.text, direct_match,
                                     measured_minus_reference)) {
            calibration_relation =
                CalibrationCorrectionRelation::MeasuredPowerMinusReference;
        }
        if (calibration_relation) {
            correction_begin = static_cast<std::size_t>(direct_match.position());
            correction_end = correction_begin +
                             static_cast<std::size_t>(direct_match.length());
        }
    }

    const bool has_calibration_indexes = !calibration_indexes.empty();
    if (has_calibration_indexes || calibration_relation) {
        const auto clause_begin = calibration_relation && has_calibration_indexes
                                      ? std::min(correction_begin, calibration_begin)
                                      : calibration_relation ? correction_begin
                                                             : calibration_begin;
        const auto clause_end = calibration_relation && has_calibration_indexes
                                    ? std::max(correction_end, calibration_end)
                                    : calibration_relation ? correction_end
                                                           : calibration_end;
        auto clause = extracted_clause(next_id("calibration"),
                                       RequirementKind::Calibration, instruction,
                                       normalized, clause_begin, clause_end);
        clause.calibration = CalibrationFact{"", "", std::move(calibration_indexes)};
        clause.calibration_correction_relation = calibration_relation;
        clause.constrain_calibration_correction = calibration_relation.has_value();
        clause.constrain_calibration_indexes = !clause.calibration->indexes.empty();
        result.push_back(std::move(clause));
    }
    if (calibration_requested && !calibration_relation) {
        const auto unknown_begin = has_calibration_indexes
                                       ? calibration_begin
                                       : static_cast<std::size_t>(
                                             calibration_request_match.position());
        const auto unknown_end = has_calibration_indexes
                                     ? calibration_end
                                     : unknown_begin + static_cast<std::size_t>(
                                                           calibration_request_match.length());
        auto clause = extracted_clause(next_id("calibration-correction"),
                                       RequirementKind::Calibration, instruction,
                                       normalized, unknown_begin, unknown_end);
        clause.confidence = RequirementConfidence::Ambiguous;
        clause.provenance = "interactive-ambiguous";
        clause.extraction_code = "calibration_correction_uncheckable";
        clause.extraction_explanation =
            "the request asks for calibration but does not state a verifiable correction direction";
        result.push_back(std::move(clause));
    }

    const std::regex assertion{
        "(absolute\\s+)?(output[- ]power|power|evm|quality|carrier(?:\\s+frequency)?\\s+error|frequency\\s+error|offset)"
        "(?:\\s+error)?(?:\\s+to\\s+be)?\\s+"
        "(strictly\\s+below|strictly\\s+less\\s+than|strictly\\s+above|"
        "strictly\\s+greater\\s+than|within|at\\s+most|no\\s+more\\s+than|"
        "no\\s+greater\\s+than|below|less\\s+than|at\\s+least|"
        "no\\s+less\\s+than|above|greater\\s+than)\\s+(" +
        number_pattern + ")\\s*(dB|%|Hz|kHz|MHz)\\b",
        std::regex_constants::icase};
    for (std::sregex_iterator match{normalized.text.begin(), normalized.text.end(),
                                    assertion}, end;
         match != end; ++match) {
        const std::string subject = lowercase((*match)[2].str());
        std::string comparison = lowercase((*match)[3].str());
        constexpr std::string_view strict_prefix{"strictly "};
        if (comparison.starts_with(strict_prefix)) {
            comparison.erase(0, strict_prefix.size());
        }
        ir::ValueType type = ir::ValueType::PowerDelta;
        if (subject == "evm" || subject == "quality") type = ir::ValueType::Ratio;
        else if (subject.find("frequency") != std::string::npos || subject == "offset" ||
                 subject.find("carrier") != std::string::npos) {
            type = ir::ValueType::FrequencyDelta;
        }
        auto converted = quantity((*match)[4].str(), (*match)[5].str());
        if (!converted) continue;
        const bool compatible =
            (type == ir::ValueType::PowerDelta &&
             std::holds_alternative<domain::PowerDelta>(*converted)) ||
            (type == ir::ValueType::Ratio &&
             std::holds_alternative<domain::Ratio>(*converted)) ||
            (type == ir::ValueType::FrequencyDelta &&
             std::holds_alternative<domain::Frequency>(*converted));
        if (!compatible) continue;
        auto clause = extracted_clause(
            next_id("assertion"), RequirementKind::Assertion, instruction,
            normalized, static_cast<std::size_t>(match->position()),
            static_cast<std::size_t>(match->position() + match->length()));
        AssertionFact fact;
        fact.operand_type = type;
        fact.right = threshold_identity(type, *converted);
        if (comparison == "below" || comparison == "less than")
            fact.op = ir::ComparisonOperator::Less;
        else if (comparison == "above" || comparison == "greater than")
            fact.op = ir::ComparisonOperator::Greater;
        else if (comparison == "at least" || comparison == "no less than")
            fact.op = ir::ComparisonOperator::GreaterEqual;
        else
            fact.op = ir::ComparisonOperator::LessEqual;
        if ((*match)[1].matched || comparison == "within") fact.left = "absolute";
        clause.constrain_assertion_left = false;
        clause.assertion = std::move(fact);
        result.push_back(std::move(clause));
    }

    if (lowered.find("power range") != std::string::npos && powers.empty()) {
        auto clause = extracted_clause(next_id("ambiguous-power-range"),
                                       RequirementKind::Sweep, instruction,
                                       normalized, 0, normalized.text.size());
        clause.extraction_code = "ambiguous_power_range";
        clause.confidence = RequirementConfidence::Ambiguous;
        clause.extraction_explanation =
            "'power range' does not unambiguously request a supported sweep or a limit";
        result.push_back(std::move(clause));
    }
    return result;
}

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
                if (requirement.constrain_sweep_bounds)
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
                const bool left_matches = requirement.constrain_assertion_left
                    ? value.left == expected.left
                    : expected.left != "absolute" ||
                          value.left.find("absolute(") != std::string::npos;
                return value.op == expected.op && value.operand_type == expected.operand_type &&
                       left_matches && value.right == expected.right;
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
            const std::string expected_correction =
                requirement.calibration_correction_relation
                    ? calibration_correction_identity(
                          *requirement.calibration_correction_relation,
                          facts.reference_power)
                    : expected.correction;
            const bool correction_matches =
                !requirement.constrain_calibration_correction ||
                facts.calibration.correction == expected_correction;
            const bool indexes_match =
                !requirement.constrain_calibration_indexes ||
                facts.calibration.indexes == expected.indexes;
            const bool match = correction_matches && indexes_match;
            item = outcome(requirement, match ? RequirementOutcomeStatus::Satisfied
                                              : RequirementOutcomeStatus::Violated,
                           match ? "calibration_match"
                                 : !correction_matches
                                     ? "calibration_correction_mismatch"
                                     : "calibration_indexes_mismatch",
                           expected_correction, facts.calibration.correction,
                           match ? "calibration requirement matches"
                                  : "calibration correction or indexes differ");
        } else {
            item = outcome(
                requirement, RequirementOutcomeStatus::Unknown,
                requirement.extraction_code.empty() ? "requirement_incomplete"
                                                    : requirement.extraction_code,
                requirement.source_text.empty() ? "typed requirement"
                                                : requirement.source_text,
                "unavailable",
                requirement.extraction_explanation.empty()
                    ? "the requirement does not contain the typed fields needed for verification"
                    : requirement.extraction_explanation);
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

RequirementVerificationResult verify_instruction_requirements(
    std::string_view instruction, const ProgramFacts& facts) {
    return verify_requirements(extract_requirements(instruction), facts);
}

} // namespace horusrf::ai
