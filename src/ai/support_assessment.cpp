#include "horusrf/ai/support_assessment.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <utility>

namespace horusrf::ai {
namespace {

std::string lower_ascii(std::string_view value) {
    std::string result{value};
    std::ranges::transform(result, result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

void add_match(std::vector<SupportClause>& clauses, std::string_view original,
               const std::smatch& match, std::string operation,
               SupportClauseStatus status, std::string code,
               std::string explanation) {
    const auto begin = static_cast<std::size_t>(match.position());
    const auto end = begin + static_cast<std::size_t>(match.length());
    const bool duplicate = std::ranges::any_of(clauses, [&](const SupportClause& item) {
        return item.operation == operation && item.source_begin == begin &&
               item.source_end == end;
    });
    if (duplicate) return;
    clauses.push_back({std::move(operation), status, std::move(code),
                       std::move(explanation), begin, end,
                       std::string{original.substr(begin, end - begin)}});
}

bool supports_sweep(const frontend::LanguageCapabilities& capabilities,
                    std::string_view dimension) {
    return std::ranges::any_of(capabilities.sweep_dimensions, [&](const auto& item) {
        return item.dimension == dimension;
    });
}

void recognize(const std::string& lower, std::string_view original,
               const std::regex& pattern, std::string operation,
               SupportClauseStatus status, std::string code,
               std::string explanation, std::vector<SupportClause>& clauses) {
    for (auto iterator = std::sregex_iterator(lower.begin(), lower.end(), pattern);
         iterator != std::sregex_iterator{}; ++iterator) {
        add_match(clauses, original, *iterator, operation, status, code, explanation);
    }
}

} // namespace

SupportAssessment assess_instruction_support(
    std::string_view instruction,
    const frontend::LanguageCapabilities& capabilities) {
    const std::string lower = lower_ascii(instruction);
    std::vector<SupportClause> clauses;

    const auto frequency_status = supports_sweep(capabilities, "frequency")
                                      ? SupportClauseStatus::Supported
                                      : SupportClauseStatus::Unsupported;
    const auto temperature_status = supports_sweep(capabilities, "temperature")
                                        ? SupportClauseStatus::Supported
                                        : SupportClauseStatus::Unsupported;
    const auto capability_explanation = [](std::string_view dimension, bool supported) {
        return std::string{dimension} +
               (supported ? " is a compiler-supported sweep dimension"
                          : " is not a compiler-supported sweep dimension");
    };

    static const std::regex frequency_sweep{
        R"(\b(?:sweep(?:\s+the)?\s+frequency|frequency\s+sweep)\b)"};
    static const std::regex temperature_sweep{
        R"(\b(?:sweep(?:\s+the)?\s+temperature|temperature\s+sweep)\b)"};
    static const std::regex power_sweep{
        R"(\b(?:sweep(?:\s+the)?(?:\s+requested)?(?:\s+transmit|\s+output)?\s+power|(?:transmit\s+|output\s+)?power\s+sweep)\b)"};
    static const std::regex fixed_frequency{
        R"(\b(?:at\s+(?:a\s+)?fixed\s+|fixed\s+frequency\b))"};
    static const std::regex ambiguous_power_range{R"(\bpower\s+range\b)"};

    recognize(lower, instruction, frequency_sweep, "sweep.frequency", frequency_status,
              frequency_status == SupportClauseStatus::Supported
                  ? "supported_sweep_dimension"
                  : "unsupported_sweep_dimension",
              capability_explanation("frequency",
                                     frequency_status == SupportClauseStatus::Supported),
              clauses);
    recognize(lower, instruction, temperature_sweep, "sweep.temperature",
              temperature_status,
              temperature_status == SupportClauseStatus::Supported
                  ? "supported_sweep_dimension"
                  : "unsupported_sweep_dimension",
              capability_explanation("temperature",
                                     temperature_status == SupportClauseStatus::Supported),
              clauses);
    recognize(lower, instruction, power_sweep, "sweep.power",
              supports_sweep(capabilities, "power") ? SupportClauseStatus::Supported
                                                      : SupportClauseStatus::Unsupported,
              supports_sweep(capabilities, "power") ? "supported_sweep_dimension"
                                                      : "unsupported_sweep_dimension",
              capability_explanation("power", supports_sweep(capabilities, "power")),
              clauses);
    recognize(lower, instruction, fixed_frequency, "fixed.frequency", frequency_status,
              frequency_status == SupportClauseStatus::Supported
                  ? "supported_fixed_frequency"
                  : "unsupported_fixed_frequency",
              frequency_status == SupportClauseStatus::Supported
                  ? "a fixed frequency is supported as a one-point frequency sweep"
                  : "fixed frequency requires compiler support for frequency sweeps",
              clauses);
    recognize(lower, instruction, ambiguous_power_range, "range.power",
              SupportClauseStatus::Uncertain, "ambiguous_power_range",
              "power range is ambiguous: it may describe a sweep or a constraint",
              clauses);

    const auto count = [&](SupportClauseStatus status) {
        return std::ranges::count_if(clauses, [=](const SupportClause& item) {
            return item.status == status;
        });
    };
    const auto supported = count(SupportClauseStatus::Supported);
    const auto unsupported = count(SupportClauseStatus::Unsupported);
    const auto uncertain = count(SupportClauseStatus::Uncertain);

    SupportAssessmentStatus status = SupportAssessmentStatus::Supported;
    const bool has_supported_primary_operation = std::ranges::any_of(
        clauses, [](const SupportClause& item) {
            return item.status == SupportClauseStatus::Supported &&
                   item.operation != "fixed.frequency";
        });
    if (unsupported != 0 && !has_supported_primary_operation && uncertain == 0) {
        status = SupportAssessmentStatus::Unsupported;
    } else if (unsupported != 0) {
        status = SupportAssessmentStatus::PartiallySupported;
    } else if (uncertain != 0) {
        status = supported != 0 ? SupportAssessmentStatus::PartiallySupported
                                : SupportAssessmentStatus::Uncertain;
    }
    return {status, std::move(clauses)};
}

} // namespace horusrf::ai
