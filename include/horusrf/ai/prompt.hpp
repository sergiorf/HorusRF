#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "horusrf/ai/contracts.hpp"
#include "horusrf/ai/provider.hpp"

namespace horusrf::ai {

enum class PromptConfiguration {
    Baseline,
    Compact,
    CapabilityRelevant,
};

[[nodiscard]] std::string_view prompt_configuration_id(
    PromptConfiguration configuration) noexcept;
[[nodiscard]] std::optional<PromptConfiguration> parse_prompt_configuration(
    std::string_view id) noexcept;

struct PromptSettings final {
    std::string model{"qwen2.5-coder:7b"};
    double temperature{0.0};
    std::size_t max_output_tokens{2048};
    std::chrono::milliseconds timeout{std::chrono::minutes{2}};
    PromptConfiguration configuration{PromptConfiguration::Baseline};
};

enum class PromptFeature {
    Frequency,
    Temperature,
    PowerMeasurement,
    EvmMeasurement,
    FrequencyErrorMeasurement,
    Assertion,
    Calibration,
    Modification,
};

struct RepairPromptRequest final {
    NaturalLanguageInstruction original_instruction;
    ScriptSource previous_candidate;
    ValidationResult validation;
    std::optional<ScriptSource> original_source;
};

struct PromptExample final {
    std::string name;
    NaturalLanguageInstruction instruction;
    std::optional<ScriptSource> current_source;
    ScriptSource proposal;
    std::string explanation;
    std::vector<PromptFeature> features;
};

// Returns the deterministic, compiler-valid examples used for few-shot context.
[[nodiscard]] const std::vector<PromptExample>& curated_prompt_examples();

// JSON Schema passed to providers for the structured proposal response.
[[nodiscard]] std::string_view proposal_response_schema() noexcept;

class PromptBuilder final {
public:
    explicit PromptBuilder(PromptSettings settings = {});

    [[nodiscard]] LlmRequest build_generation(const GenerationRequest& request) const;
    [[nodiscard]] LlmRequest build_modification(const ModificationRequest& request) const;
    [[nodiscard]] LlmRequest build_repair(const RepairPromptRequest& request) const;

private:
    [[nodiscard]] LlmRequest base_request() const;

    PromptSettings settings_;
};

struct ProposalPayload final {
    ScriptSource script;
    std::string explanation;
    std::vector<std::string> warnings;
};

enum class ProposalResponseErrorCode { MalformedJson, SchemaViolation };

struct ProposalResponseError final {
    ProposalResponseErrorCode code{ProposalResponseErrorCode::MalformedJson};
    std::string message;
};

using ProposalResponseResult = std::variant<ProposalPayload, ProposalResponseError>;

// Parses and enforces the same strict response shape as proposal_response_schema().
// This verifies provider output shape only; callers must still validate the script.
[[nodiscard]] ProposalResponseResult parse_proposal_response(std::string_view response);

// Conservatively reports explicit number-and-unit requirements that do not appear
// in a candidate. These warnings require user review; they do not override the
// compiler's validity decision and equivalent unit conversions may be acceptable.
[[nodiscard]] std::vector<std::string> numeric_fidelity_warnings(
    const NaturalLanguageInstruction& instruction, const ScriptSource& candidate);

} // namespace horusrf::ai
