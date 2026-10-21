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

enum class EvaluationGenerationStatus { Completed, Failed, Cancelled };

struct EvaluationError final {
    AgentErrorCode code{AgentErrorCode::Internal};
    std::string message;
};

struct CaseEvaluation final {
    std::string case_id;
    std::string category;
    std::string model;
    std::size_t repetition{1};
    bool expected_supported{};
    EvaluationGenerationStatus generation_status{EvaluationGenerationStatus::Failed};
    bool first_attempt_valid{};
    bool final_valid{};
    SemanticComparisonStatus semantic_status{SemanticComparisonStatus::NotEvaluated};
    bool unsupported_recognized{};
    std::size_t repair_attempts{};
    std::chrono::milliseconds total_latency{};
    std::vector<std::optional<ProviderMetrics>> provider_attempts;
    std::optional<ProviderIdentity> provider_identity;
    std::optional<EvaluationError> error;
    std::optional<std::string> generated_source;
};

struct ModelEvaluationSummary final {
    std::string model;
    std::size_t total_cases{};
    std::size_t supported_cases{};
    std::size_t unsupported_cases{};
    std::size_t generation_completed{};
    std::size_t first_attempt_valid{};
    std::size_t final_valid{};
    std::size_t semantic_matches{};
    std::size_t semantic_mismatches{};
    std::size_t semantic_not_evaluated{};
    std::size_t unsupported_recognized{};
    double average_repair_attempts{};
    std::chrono::milliseconds median_latency{};
    std::chrono::milliseconds p90_latency{};
    std::optional<double> average_output_tokens_per_second;
    std::vector<std::pair<std::string, std::size_t>> failure_categories;
};

struct EvaluationReport final {
    std::string dataset_version;
    std::string timestamp_utc;
    std::optional<std::string> git_revision;
    double temperature{};
    std::size_t max_output_tokens{};
    std::chrono::milliseconds timeout{};
    std::size_t maximum_repair_attempts{};
    std::vector<CaseEvaluation> cases;
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
};

using EvaluationClock =
    std::function<std::chrono::steady_clock::time_point()>;

// Runs authoring only: generated scripts are compiled and compared but never
// executed through runtime, simulator, or hardware layers.
[[nodiscard]] EvaluationReport evaluate_golden_dataset(
    const GoldenDataset& dataset, LlmProvider& provider,
    const EvaluationSettings& settings, CancellationToken cancellation = {},
    EvaluationClock clock = [] { return std::chrono::steady_clock::now(); });

void write_evaluation_json(std::ostream& output, const EvaluationReport& report);
void write_evaluation_table(std::ostream& output, const EvaluationReport& report);

} // namespace horusrf::ai
