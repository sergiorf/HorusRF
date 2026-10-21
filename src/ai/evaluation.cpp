#include "horusrf/ai/evaluation.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <map>
#include <numeric>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

#include "horusrf/ai/validation.hpp"

namespace horusrf::ai {
namespace {

class ObservingProvider final : public LlmProvider {
public:
    explicit ObservingProvider(LlmProvider& provider) : provider_(provider) {}

    LlmResult generate(const LlmRequest& request,
                       CancellationToken cancellation) override {
        LlmResult result = provider_.generate(request, cancellation);
        if (const auto* response = std::get_if<ProviderResponse>(&result)) {
            attempts.push_back(response->metrics);
            identity = response->identity;
        } else {
            attempts.push_back(std::nullopt);
        }
        return result;
    }

    LlmProvider& provider_;
    std::vector<std::optional<ProviderMetrics>> attempts;
    std::optional<ProviderIdentity> identity;
};

std::string lower_ascii(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool reports_unsupported(const ScriptProposal& proposal) {
    std::string text = proposal.explanation();
    for (const auto& warning : proposal.warnings()) {
        text.push_back('\n');
        text += warning;
    }
    text = lower_ascii(std::move(text));
    return text.find("unsupported") != std::string::npos ||
           text.find("not supported") != std::string::npos ||
           text.find("cannot") != std::string::npos ||
           text.find("unable") != std::string::npos;
}

std::string_view generation_name(EvaluationGenerationStatus status) {
    switch (status) {
    case EvaluationGenerationStatus::Completed: return "completed";
    case EvaluationGenerationStatus::Failed: return "failed";
    case EvaluationGenerationStatus::Cancelled: return "cancelled";
    }
    return "failed";
}

std::string_view semantic_name(SemanticComparisonStatus status) {
    switch (status) {
    case SemanticComparisonStatus::Match: return "match";
    case SemanticComparisonStatus::Mismatch: return "mismatch";
    case SemanticComparisonStatus::NotEvaluated: return "not_evaluated";
    }
    return "not_evaluated";
}

std::string_view error_name(AgentErrorCode code) {
    switch (code) {
    case AgentErrorCode::InvalidRequest: return "invalid_request";
    case AgentErrorCode::ProviderFailure: return "provider_failure";
    case AgentErrorCode::ValidationFailure: return "validation_failure";
    case AgentErrorCode::RepairLimitReached: return "repair_limit_reached";
    case AgentErrorCode::Cancelled: return "cancelled";
    case AgentErrorCode::Internal: return "internal";
    }
    return "internal";
}

std::string json_string(std::string_view value) {
    std::ostringstream output;
    output << '"';
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20) {
                output << "\\u00" << hex[character >> 4] << hex[character & 0x0f];
            } else {
                output << static_cast<char>(character);
            }
        }
    }
    output << '"';
    return output.str();
}

template <typename T>
void write_optional_number(std::ostream& output, const std::optional<T>& value) {
    if (value) output << *value;
    else output << "null";
}

void write_optional_duration(std::ostream& output,
                             const std::optional<std::chrono::nanoseconds>& value) {
    if (value) output << value->count();
    else output << "null";
}

ModelEvaluationSummary summarize(std::string model,
                                 const std::vector<CaseEvaluation>& cases) {
    ModelEvaluationSummary result;
    result.model = std::move(model);
    std::vector<std::chrono::milliseconds> latencies;
    std::size_t total_repairs{};
    double output_tokens{};
    double generation_seconds{};
    for (const auto& item : cases) {
        if (item.model != result.model) continue;
        ++result.total_cases;
        item.expected_supported ? ++result.supported_cases : ++result.unsupported_cases;
        if (item.generation_status == EvaluationGenerationStatus::Completed) {
            ++result.generation_completed;
        }
        result.first_attempt_valid += item.first_attempt_valid ? 1U : 0U;
        result.final_valid += item.final_valid ? 1U : 0U;
        result.semantic_matches +=
            item.semantic_status == SemanticComparisonStatus::Match ? 1U : 0U;
        result.semantic_mismatches +=
            item.semantic_status == SemanticComparisonStatus::Mismatch ? 1U : 0U;
        result.semantic_not_evaluated +=
            item.semantic_status == SemanticComparisonStatus::NotEvaluated ? 1U : 0U;
        result.unsupported_recognized += item.unsupported_recognized ? 1U : 0U;
        if (item.error) {
            const std::string category{error_name(item.error->code)};
            const auto found = std::ranges::find(result.failure_categories, category,
                                                 &std::pair<std::string, std::size_t>::first);
            if (found == result.failure_categories.end()) {
                result.failure_categories.emplace_back(category, 1);
            } else {
                ++found->second;
            }
        }
        total_repairs += item.repair_attempts;
        latencies.push_back(item.total_latency);
        for (const auto& attempt : item.provider_attempts) {
            if (attempt && attempt->output_token_count && attempt->generation_duration &&
                attempt->generation_duration->count() > 0) {
                output_tokens += static_cast<double>(*attempt->output_token_count);
                generation_seconds +=
                    std::chrono::duration<double>(*attempt->generation_duration).count();
            }
        }
    }
    if (result.total_cases != 0) {
        result.average_repair_attempts =
            static_cast<double>(total_repairs) / static_cast<double>(result.total_cases);
    }
    std::ranges::sort(latencies);
    if (!latencies.empty()) {
        const auto middle = latencies.size() / 2;
        if (latencies.size() % 2 == 0) {
            result.median_latency = std::chrono::milliseconds{
                (latencies[middle - 1].count() + latencies[middle].count()) / 2};
        } else {
            result.median_latency = latencies[middle];
        }
        const auto p90_index = static_cast<std::size_t>(
            std::ceil(static_cast<double>(latencies.size()) * 0.9) - 1.0);
        result.p90_latency = latencies[p90_index];
    }
    if (generation_seconds > 0.0) {
        result.average_output_tokens_per_second = output_tokens / generation_seconds;
    }
    std::ranges::sort(result.failure_categories, {},
                      &std::pair<std::string, std::size_t>::first);
    return result;
}

} // namespace

EvaluationReport evaluate_golden_dataset(const GoldenDataset& dataset,
                                         LlmProvider& provider,
                                         const EvaluationSettings& settings,
                                         CancellationToken cancellation,
                                         EvaluationClock clock) {
    if (dataset.cases.empty()) throw std::invalid_argument("at least one golden case is required");
    if (settings.models.empty()) throw std::invalid_argument("at least one model is required");
    if (settings.repetitions == 0) throw std::invalid_argument("repetitions must be positive");
    if (!std::isfinite(settings.temperature) || settings.temperature < 0.0) {
        throw std::invalid_argument("temperature must be finite and non-negative");
    }
    if (settings.max_output_tokens == 0) {
        throw std::invalid_argument("maximum output tokens must be positive");
    }
    if (settings.timeout <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("timeout must be positive");
    }
    if (!clock) throw std::invalid_argument("an evaluation clock is required");

    std::set<std::string, std::less<>> distinct_models;
    for (const auto& model : settings.models) {
        if (model.empty()) throw std::invalid_argument("model identifiers must not be empty");
        if (!distinct_models.insert(model).second) {
            throw std::invalid_argument("model identifiers must be unique");
        }
    }

    EvaluationReport report{dataset.dataset_version,
                            settings.timestamp_utc,
                            settings.git_revision,
                            settings.temperature,
                            settings.max_output_tokens,
                            settings.timeout,
                            settings.maximum_repair_attempts,
                            {}, {}};
    for (const auto& model : settings.models) {
        for (std::size_t repetition = 1; repetition <= settings.repetitions; ++repetition) {
            for (const auto& golden : dataset.cases) {
                CaseEvaluation item;
                item.case_id = golden.metadata.id;
                item.category = golden.metadata.category;
                item.model = model;
                item.repetition = repetition;
                item.expected_supported = golden.metadata.expected_valid;

                ObservingProvider observed{provider};
                PromptBuilder prompts{{model, settings.temperature,
                                       settings.max_output_tokens, settings.timeout}};
                AgentSession session{observed, std::move(prompts),
                                     {settings.maximum_repair_attempts}};
                const auto started = clock();
                const AgentResult agent_result = session.generate(
                    {NaturalLanguageInstruction{golden.prompt}}, cancellation);
                const auto finished = clock();
                item.total_latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                    finished - started);
                item.provider_attempts = std::move(observed.attempts);
                item.provider_identity = std::move(observed.identity);
                item.repair_attempts = item.provider_attempts.empty()
                                           ? 0
                                           : item.provider_attempts.size() - 1;

                const ScriptProposal* proposal = std::get_if<ScriptProposal>(&agent_result);
                if (proposal != nullptr) {
                    item.generation_status = EvaluationGenerationStatus::Completed;
                } else {
                    const auto& failure = std::get<AgentFailure>(agent_result);
                    item.generation_status = failure.error.code == AgentErrorCode::Cancelled
                                                 ? EvaluationGenerationStatus::Cancelled
                                                 : EvaluationGenerationStatus::Failed;
                    item.error = EvaluationError{failure.error.code, failure.error.message};
                    if (failure.proposal) proposal = &*failure.proposal;
                }

                if (proposal != nullptr) {
                    item.generated_source = proposal->candidate_source().text();
                    const auto validation = validate_script(*item.generated_source);
                    item.final_valid = validation.ok();
                    item.first_attempt_valid = item.final_valid &&
                                               item.provider_attempts.size() == 1;
                    if (golden.metadata.expected_valid && item.final_valid) {
                        item.semantic_status = compare_script_semantics(
                                                   golden.expected_source,
                                                   *item.generated_source)
                                                   .status;
                    }
                    if (!golden.metadata.expected_valid) {
                        item.unsupported_recognized = reports_unsupported(*proposal);
                    }
                }
                report.cases.push_back(std::move(item));
                if (cancellation.stop_requested()) break;
            }
            if (cancellation.stop_requested()) break;
        }
        if (cancellation.stop_requested()) break;
    }
    for (const auto& model : settings.models) {
        report.summaries.push_back(summarize(model, report.cases));
    }
    return report;
}

void write_evaluation_json(std::ostream& output, const EvaluationReport& report) {
    output << "{\"schema\":\"horusrf.ai-evaluation\",\"version\":1"
           << ",\"dataset_version\":" << json_string(report.dataset_version)
           << ",\"timestamp_utc\":" << json_string(report.timestamp_utc)
           << ",\"git_revision\":";
    if (report.git_revision) output << json_string(*report.git_revision);
    else output << "null";
    output << ",\"settings\":{\"temperature\":" << report.temperature
           << ",\"max_output_tokens\":" << report.max_output_tokens
           << ",\"timeout_ms\":" << report.timeout.count()
           << ",\"maximum_repair_attempts\":" << report.maximum_repair_attempts
           << "},\"cases\":[";
    for (std::size_t index = 0; index < report.cases.size(); ++index) {
        if (index != 0) output << ',';
        const auto& item = report.cases[index];
        output << "{\"case_id\":" << json_string(item.case_id)
               << ",\"category\":" << json_string(item.category)
               << ",\"model\":" << json_string(item.model)
               << ",\"repetition\":" << item.repetition
               << ",\"expected_supported\":" << (item.expected_supported ? "true" : "false")
               << ",\"generation_status\":" << json_string(generation_name(item.generation_status))
               << ",\"first_attempt_valid\":" << (item.first_attempt_valid ? "true" : "false")
               << ",\"final_valid\":" << (item.final_valid ? "true" : "false")
               << ",\"semantic_status\":" << json_string(semantic_name(item.semantic_status))
               << ",\"unsupported_recognized\":" << (item.unsupported_recognized ? "true" : "false")
               << ",\"repair_attempts\":" << item.repair_attempts
               << ",\"total_latency_ms\":" << item.total_latency.count()
               << ",\"provider_identity\":";
        if (item.provider_identity) {
            output << "{\"provider\":" << json_string(item.provider_identity->provider)
                   << ",\"model\":" << json_string(item.provider_identity->model) << '}';
        } else output << "null";
        output
               << ",\"provider_attempts\":[";
        for (std::size_t attempt = 0; attempt < item.provider_attempts.size(); ++attempt) {
            if (attempt != 0) output << ',';
            if (!item.provider_attempts[attempt]) {
                output << "null";
                continue;
            }
            const auto& metrics = *item.provider_attempts[attempt];
            output << "{\"model_load_duration_ns\":";
            write_optional_duration(output, metrics.model_load_duration);
            output << ",\"prompt_evaluation_duration_ns\":";
            write_optional_duration(output, metrics.prompt_evaluation_duration);
            output << ",\"generation_duration_ns\":";
            write_optional_duration(output, metrics.generation_duration);
            output << ",\"input_token_count\":";
            write_optional_number(output, metrics.input_token_count);
            output << ",\"output_token_count\":";
            write_optional_number(output, metrics.output_token_count);
            output << '}';
        }
        output << "],\"error\":";
        if (item.error) {
            output << "{\"code\":" << json_string(error_name(item.error->code))
                   << ",\"message\":" << json_string(item.error->message) << '}';
        } else output << "null";
        output << ",\"generated_source\":";
        if (item.generated_source) output << json_string(*item.generated_source);
        else output << "null";
        output << '}';
    }
    output << "],\"summaries\":[";
    for (std::size_t index = 0; index < report.summaries.size(); ++index) {
        if (index != 0) output << ',';
        const auto& item = report.summaries[index];
        output << "{\"model\":" << json_string(item.model)
               << ",\"total_cases\":" << item.total_cases
               << ",\"supported_cases\":" << item.supported_cases
               << ",\"unsupported_cases\":" << item.unsupported_cases
               << ",\"generation_completed\":" << item.generation_completed
               << ",\"first_attempt_valid\":" << item.first_attempt_valid
               << ",\"final_valid\":" << item.final_valid
               << ",\"semantic_matches\":" << item.semantic_matches
               << ",\"semantic_mismatches\":" << item.semantic_mismatches
               << ",\"semantic_not_evaluated\":" << item.semantic_not_evaluated
               << ",\"unsupported_recognized\":" << item.unsupported_recognized
               << ",\"average_repair_attempts\":" << item.average_repair_attempts
               << ",\"median_latency_ms\":" << item.median_latency.count()
               << ",\"p90_latency_ms\":" << item.p90_latency.count()
               << ",\"average_output_tokens_per_second\":";
        write_optional_number(output, item.average_output_tokens_per_second);
        output << ",\"rates\":{\"generation_completed\":{\"numerator\":"
               << item.generation_completed << ",\"denominator\":" << item.total_cases
               << "},\"first_attempt_valid\":{\"numerator\":"
               << item.first_attempt_valid << ",\"denominator\":" << item.total_cases
               << "},\"final_valid\":{\"numerator\":" << item.final_valid
               << ",\"denominator\":" << item.total_cases
               << "},\"semantic_matches\":{\"numerator\":" << item.semantic_matches
               << ",\"denominator\":" << item.supported_cases
               << "},\"unsupported_recognized\":{\"numerator\":"
               << item.unsupported_recognized << ",\"denominator\":"
               << item.unsupported_cases << "}},\"failure_categories\":{";
        for (std::size_t failure = 0; failure < item.failure_categories.size(); ++failure) {
            if (failure != 0) output << ',';
            output << json_string(item.failure_categories[failure].first) << ':'
                   << item.failure_categories[failure].second;
        }
        output << '}';
        output << '}';
    }
    output << "]}\n";
}

void write_evaluation_table(std::ostream& output, const EvaluationReport& report) {
    output << "Model                       Cases  First valid  Final valid  Semantic  Median ms  P90 ms\n";
    output << "---------------------------------------------------------------------------------------\n";
    for (const auto& item : report.summaries) {
        output << std::left << std::setw(27) << item.model << std::right
               << std::setw(6) << item.total_cases << std::setw(13)
               << item.first_attempt_valid << std::setw(13) << item.final_valid
               << std::setw(10) << item.semantic_matches << std::setw(11)
               << item.median_latency.count() << std::setw(8)
               << item.p90_latency.count() << '\n';
    }
    output << "Semantic counts use supported golden cases; unsupported recognition is reported in JSON.\n";
}

} // namespace horusrf::ai
