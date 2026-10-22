#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "horusrf/ai/agent.hpp"
#include "horusrf/ai/golden_dataset.hpp"
#include "horusrf/ai/semantic_comparison.hpp"

namespace horusrf::ai {

enum class EvaluationGenerationStatus { Completed, Unsupported, Failed, Cancelled };
enum class EvaluationCallPurpose { Generation, CompilerRepair, RequirementCorrection };
enum class EvaluationProgressStage { Started, Completed };
enum class StructuredResponseStatus {
    Parsed,
    MalformedJson,
    SchemaViolation,
    ProviderError,
    ProviderException,
};
enum class WarmColdStatus { Cold, Warm, Unknown };

struct EvaluationError final {
    AgentErrorCode code{AgentErrorCode::Internal};
    std::string message;
};

struct PromptSectionSize final {
    std::string section;
    std::size_t utf8_bytes{};
};

struct PromptSize final {
    std::size_t total_utf8_bytes{};
    std::vector<PromptSectionSize> sections;
};

struct EvaluationAttempt final {
    std::size_t number{};
    EvaluationCallPurpose purpose{EvaluationCallPurpose::Generation};
    PromptSize prompt_size;
    StructuredResponseStatus structured_response_status{
        StructuredResponseStatus::ProviderError};
    std::optional<ProviderErrorCode> provider_error_code;
    std::optional<std::string> response_error;
    std::optional<std::string> candidate_source;
    std::optional<std::string> candidate_hash;
    bool repeated_candidate{};
    std::optional<bool> diagnostic_set_changed;
    std::optional<std::string> explanation;
    std::vector<std::string> warnings;
    std::optional<bool> compiler_valid;
    std::vector<ValidationDiagnostic> compiler_diagnostics;
    SemanticComparisonStatus semantic_status{SemanticComparisonStatus::NotEvaluated};
    std::vector<SemanticDifference> semantic_differences;
    RequirementVerificationResult requirement_verification;
    std::optional<std::chrono::nanoseconds> compiler_duration;
    std::optional<std::chrono::nanoseconds> verification_duration;
    std::optional<ProviderMetrics> provider_metrics;
    std::optional<ProviderIdentity> provider_identity;
};

struct LatencyStatistics final {
    std::size_t sample_count{};
    std::chrono::milliseconds median{};
    std::chrono::milliseconds p90{};
};

struct CaseEvaluation final {
    std::string case_id;
    std::string category;
    std::string model;
    std::size_t repetition{1};
    std::size_t run_order{};
    WarmColdStatus warm_cold_status{WarmColdStatus::Unknown};
    bool expected_supported{};
    SupportAssessmentStatus expected_support{SupportAssessmentStatus::Supported};
    EvaluationGenerationStatus generation_status{EvaluationGenerationStatus::Failed};
    bool first_attempt_valid{};
    bool final_valid{};
    SemanticComparisonStatus semantic_status{SemanticComparisonStatus::NotEvaluated};
    RequirementVerificationResult requirement_verification;
    SupportAssessment support_assessment;
    bool unsupported_recognized{};
    bool support_assessment_matches{};
    std::size_t repair_attempts{};
    std::chrono::milliseconds total_latency{};
    std::vector<std::optional<ProviderMetrics>> provider_attempts;
    std::vector<EvaluationAttempt> attempts;
    std::optional<ProviderIdentity> provider_identity;
    std::optional<EvaluationError> error;
    std::optional<std::string> generated_source;
    std::vector<std::string> final_warnings;
};

struct CaseEvaluationTrend final {
    std::string case_id;
    std::string category;
    std::string model;
    SupportAssessmentStatus expected_support{SupportAssessmentStatus::Supported};
    std::size_t repetitions{};
    std::size_t final_valid{};
    std::size_t requirement_satisfied{};
    std::size_t semantic_matches{};
    std::size_t support_assessment_matches{};
    std::size_t runs_with_repair{};
    std::size_t runs_with_requirement_correction{};
    bool persistent_requirement_failure{};
    LatencyStatistics latency;
};

struct ModelEvaluationSummary final {
    std::string model;
    std::size_t total_cases{};
    std::size_t applicable_cases{};
    std::size_t supported_cases{};
    std::size_t partially_supported_cases{};
    std::size_t unsupported_cases{};
    std::size_t uncertain_cases{};
    std::size_t generation_completed{};
    std::size_t first_attempt_valid{};
    std::size_t final_valid{};
    std::size_t semantic_matches{};
    std::size_t semantic_mismatches{};
    std::size_t semantic_not_evaluated{};
    std::size_t requirement_satisfied{};
    std::size_t requirement_violated{};
    std::size_t requirement_unknown{};
    std::size_t requirement_not_evaluated{};
    std::size_t unsupported_recognized{};
    std::size_t support_assessment_matches{};
    std::size_t distinct_cases{};
    std::size_t repetition_count{};
    double median_case_requirement_success_rate{};
    double repetition_success_rate_standard_deviation{};
    double repair_rate{};
    double requirement_correction_rate{};
    double average_repair_attempts{};
    std::chrono::milliseconds median_latency{};
    std::chrono::milliseconds p90_latency{};
    LatencyStatistics cold_latency;
    LatencyStatistics warm_latency;
    std::size_t output_limit_errors{};
    std::optional<double> average_output_tokens_per_second;
    std::vector<std::pair<std::string, std::size_t>> failure_categories;
};

struct EvaluationReport final {
    std::string prompt_configuration_id;
    std::string comparison_version;
    std::string requirement_schema_version;
    std::string scoring_version;
    std::string dataset_version;
    std::string timestamp_utc;
    std::optional<std::string> git_revision;
    double temperature{};
    std::size_t max_output_tokens{};
    std::chrono::milliseconds timeout{};
    std::size_t maximum_repair_attempts{};
    std::vector<CaseEvaluation> cases;
    std::vector<CaseEvaluationTrend> case_trends;
    std::vector<ModelEvaluationSummary> summaries;
};

struct EvaluationSettings final {
    std::vector<std::string> models;
    double temperature{0.0};
    std::size_t max_output_tokens{2048};
    std::chrono::milliseconds timeout{std::chrono::minutes{2}};
    std::size_t maximum_repair_attempts{3};
    std::size_t repetitions{1};
    std::string timestamp_utc;
    std::optional<std::string> git_revision;
    std::string prompt_configuration_id{"direct-dsl-v1"};
    std::string comparison_version{"semantic-ir-v2"};
};

using EvaluationClock =
    std::function<std::chrono::steady_clock::time_point()>;
using EvaluationProgressCallback = std::function<void(
    EvaluationProgressStage stage, const CaseEvaluation& item,
    std::size_t completed, std::size_t total)>;

// Runs authoring only: generated scripts are compiled and compared but never
// executed through runtime, simulator, or hardware layers.
[[nodiscard]] EvaluationReport evaluate_golden_dataset(
    const GoldenDataset& dataset, LlmProvider& provider,
    const EvaluationSettings& settings, CancellationToken cancellation = {},
    EvaluationClock clock = [] { return std::chrono::steady_clock::now(); },
    EvaluationProgressCallback progress = {});

void write_evaluation_json(std::ostream& output, const EvaluationReport& report);
void write_evaluation_table(std::ostream& output, const EvaluationReport& report);

} // namespace horusrf::ai
