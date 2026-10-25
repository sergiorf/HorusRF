#include "horusrf/ai/evaluation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

EvaluationCallPurpose evaluation_purpose(LlmCallPurpose purpose) {
    switch (purpose) {
    case LlmCallPurpose::Generation: return EvaluationCallPurpose::Generation;
    case LlmCallPurpose::CompilerRepair: return EvaluationCallPurpose::CompilerRepair;
    case LlmCallPurpose::RequirementCorrection:
        return EvaluationCallPurpose::RequirementCorrection;
    }
    return EvaluationCallPurpose::Generation;
}

std::string stable_source_hash(std::string_view source) {
    std::uint64_t value = 14695981039346656037ULL;
    for (const unsigned char byte : source) {
        value ^= byte;
        value *= 1099511628211ULL;
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << value;
    return output.str();
}

std::vector<std::string> diagnostic_set(
    const std::vector<ValidationDiagnostic>& diagnostics) {
    std::vector<std::string> result;
    result.reserve(diagnostics.size());
    for (const auto& diagnostic : diagnostics) {
        result.push_back(diagnostic.code + "\n" + diagnostic.message + "\n" +
                         std::to_string(diagnostic.span.begin.line) + ":" +
                         std::to_string(diagnostic.span.begin.column) + "-" +
                         std::to_string(diagnostic.span.end.line) + ":" +
                         std::to_string(diagnostic.span.end.column));
    }
    std::ranges::sort(result);
    return result;
}

class ObservingProvider final : public LlmProvider {
public:
    explicit ObservingProvider(LlmProvider& provider, EvaluationClock clock)
        : provider_(provider), clock_(std::move(clock)) {}

    LlmResult generate(const LlmRequest& request,
                       CancellationToken cancellation) override {
        EvaluationAttempt attempt;
        attempt.number = attempts.size() + 1;
        attempt.purpose = evaluation_purpose(request.purpose);
        for (std::size_t index = 0; index < request.messages.size(); ++index) {
            const auto& message = request.messages[index];
            std::string section = "message[" + std::to_string(index) + "].";
            switch (message.role) {
            case LlmRole::System: section += "system"; break;
            case LlmRole::User: section += "user"; break;
            case LlmRole::Assistant: section += "assistant"; break;
            }
            attempt.prompt_size.sections.push_back(
                {std::move(section), message.content.size()});
            attempt.prompt_size.total_utf8_bytes += message.content.size();
        }
        if (request.json_schema) {
            attempt.prompt_size.sections.push_back(
                {"response_schema", request.json_schema->size()});
            attempt.prompt_size.total_utf8_bytes += request.json_schema->size();
        }

        LlmResult result;
        try {
            result = provider_.generate(request, cancellation);
        } catch (const std::exception& error) {
            attempt.structured_response_status =
                StructuredResponseStatus::ProviderException;
            attempt.response_error = error.what();
            attempts.push_back(std::move(attempt));
            response_texts_.push_back(std::nullopt);
            throw;
        } catch (...) {
            attempt.structured_response_status =
                StructuredResponseStatus::ProviderException;
            attempt.response_error = "non-standard provider exception";
            attempts.push_back(std::move(attempt));
            response_texts_.push_back(std::nullopt);
            throw;
        }
        if (const auto* response = std::get_if<ProviderResponse>(&result)) {
            attempt.provider_metrics = response->metrics;
            attempt.provider_identity = response->identity;
            identity = response->identity;
            response_texts_.push_back(response->text);
        } else {
            attempt.structured_response_status = StructuredResponseStatus::ProviderError;
            const auto& error = std::get<ProviderError>(result);
            attempt.provider_error_code = error.code;
            attempt.response_error = error.message;
            response_texts_.push_back(std::nullopt);
        }
        attempts.push_back(std::move(attempt));
        return result;
    }

    void finalize(std::string_view expected_source, bool compare_semantics,
                  const std::vector<RequirementClause>& requirements) {
        std::set<std::string, std::less<>> seen_candidates;
        std::optional<std::vector<std::string>> previous_diagnostics;
        for (std::size_t index = 0; index < attempts.size(); ++index) {
            if (index >= response_texts_.size() || !response_texts_[index]) continue;
            auto& attempt = attempts[index];
            auto parsed = parse_proposal_response(*response_texts_[index]);
            if (const auto* error = std::get_if<ProposalResponseError>(&parsed)) {
                attempt.structured_response_status =
                    error->code == ProposalResponseErrorCode::MalformedJson
                        ? StructuredResponseStatus::MalformedJson
                        : StructuredResponseStatus::SchemaViolation;
                attempt.response_error = error->message;
                continue;
            }
            attempt.structured_response_status = StructuredResponseStatus::Parsed;
            const auto& payload = std::get<ProposalPayload>(parsed);
            attempt.candidate_source = payload.script.text();
            attempt.candidate_hash = stable_source_hash(payload.script.text());
            attempt.repeated_candidate =
                !seen_candidates.insert(payload.script.text()).second;
            attempt.explanation = payload.explanation;
            attempt.warnings = payload.warnings;
            if (payload.script.text().empty()) continue;
            const auto compiler_started = clock_();
            const auto validation = validate_script(payload.script.text());
            const auto compiler_finished = clock_();
            attempt.compiler_duration =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    compiler_finished - compiler_started);
            attempt.compiler_valid = validation.ok();
            attempt.compiler_diagnostics = validation.diagnostics();
            const auto current_diagnostics = diagnostic_set(validation.diagnostics());
            if (previous_diagnostics) {
                attempt.diagnostic_set_changed =
                    current_diagnostics != *previous_diagnostics;
            }
            previous_diagnostics = current_diagnostics;
            if (compare_semantics && validation.ok()) {
                const auto comparison = compare_script_semantics(
                    expected_source, payload.script.text());
                attempt.semantic_status = comparison.status;
                attempt.semantic_differences = comparison.differences;
                const auto verification_started = clock_();
                const auto facts = compile_program_facts(payload.script.text());
                if (facts) {
                    attempt.requirement_verification =
                        verify_requirements(requirements, *facts);
                }
                const auto verification_finished = clock_();
                attempt.verification_duration =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        verification_finished - verification_started);
            }
        }
        response_texts_.clear();
    }

    LlmProvider& provider_;
    EvaluationClock clock_;
    std::vector<EvaluationAttempt> attempts;
    std::optional<ProviderIdentity> identity;

private:
    std::vector<std::optional<std::string>> response_texts_;
};

std::string_view generation_name(EvaluationGenerationStatus status) {
    switch (status) {
    case EvaluationGenerationStatus::Completed: return "completed";
    case EvaluationGenerationStatus::Unsupported: return "unsupported";
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

std::string_view requirement_status_name(RequirementSatisfactionStatus status) {
    switch (status) {
    case RequirementSatisfactionStatus::Satisfied: return "satisfied";
    case RequirementSatisfactionStatus::Violated: return "violated";
    case RequirementSatisfactionStatus::Unknown: return "unknown";
    case RequirementSatisfactionStatus::Unsupported: return "unsupported";
    case RequirementSatisfactionStatus::NotEvaluated: return "not_evaluated";
    }
    return "not_evaluated";
}

std::string_view requirement_outcome_name(RequirementOutcomeStatus status) {
    switch (status) {
    case RequirementOutcomeStatus::Satisfied: return "satisfied";
    case RequirementOutcomeStatus::Violated: return "violated";
    case RequirementOutcomeStatus::Unknown: return "unknown";
    case RequirementOutcomeStatus::Unsupported: return "unsupported";
    }
    return "unknown";
}

std::string_view support_status_name(SupportAssessmentStatus status) {
    switch (status) {
    case SupportAssessmentStatus::Supported: return "supported";
    case SupportAssessmentStatus::PartiallySupported: return "partially_supported";
    case SupportAssessmentStatus::Unsupported: return "unsupported";
    case SupportAssessmentStatus::Uncertain: return "uncertain";
    }
    return "uncertain";
}

std::string_view support_clause_status_name(SupportClauseStatus status) {
    switch (status) {
    case SupportClauseStatus::Supported: return "supported";
    case SupportClauseStatus::Unsupported: return "unsupported";
    case SupportClauseStatus::Uncertain: return "uncertain";
    }
    return "uncertain";
}

std::string_view call_purpose_name(EvaluationCallPurpose purpose) {
    switch (purpose) {
    case EvaluationCallPurpose::Generation: return "generation";
    case EvaluationCallPurpose::CompilerRepair: return "compiler_repair";
    case EvaluationCallPurpose::RequirementCorrection: return "requirement_correction";
    }
    return "generation";
}

std::string_view structured_response_name(StructuredResponseStatus status) {
    switch (status) {
    case StructuredResponseStatus::Parsed: return "parsed";
    case StructuredResponseStatus::MalformedJson: return "malformed_json";
    case StructuredResponseStatus::SchemaViolation: return "schema_violation";
    case StructuredResponseStatus::ProviderError: return "provider_error";
    case StructuredResponseStatus::ProviderException: return "provider_exception";
    }
    return "provider_error";
}

std::string_view provider_error_name(ProviderErrorCode code) {
    switch (code) {
    case ProviderErrorCode::InvalidRequest: return "invalid_request";
    case ProviderErrorCode::InvalidEndpoint: return "invalid_endpoint";
    case ProviderErrorCode::Unavailable: return "unavailable";
    case ProviderErrorCode::ModelUnavailable: return "model_unavailable";
    case ProviderErrorCode::Timeout: return "timeout";
    case ProviderErrorCode::Cancelled: return "cancelled";
    case ProviderErrorCode::MalformedResponse: return "malformed_response";
    case ProviderErrorCode::EmptyResponse: return "empty_response";
    case ProviderErrorCode::ContextLimit: return "context_limit";
    case ProviderErrorCode::OutputLimit: return "output_limit";
    case ProviderErrorCode::UnexpectedResponse: return "unexpected_response";
    }
    return "unexpected_response";
}

std::string_view warm_cold_name(WarmColdStatus status) {
    switch (status) {
    case WarmColdStatus::Cold: return "cold";
    case WarmColdStatus::Warm: return "warm";
    case WarmColdStatus::Unknown: return "unknown";
    }
    return "unknown";
}

std::string_view diagnostic_category_name(DiagnosticCategory category) {
    switch (category) {
    case DiagnosticCategory::Syntax: return "syntax";
    case DiagnosticCategory::Semantic: return "semantic";
    case DiagnosticCategory::Lowering: return "lowering";
    }
    return "unknown";
}

std::string_view error_name(AgentErrorCode code) {
    switch (code) {
    case AgentErrorCode::InvalidRequest: return "invalid_request";
    case AgentErrorCode::ProviderFailure: return "provider_failure";
    case AgentErrorCode::ValidationFailure: return "validation_failure";
    case AgentErrorCode::RepairLimitReached: return "repair_limit_reached";
    case AgentErrorCode::NoProgress: return "no_progress";
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

void write_provider_metrics(std::ostream& output,
                            const std::optional<ProviderMetrics>& value) {
    if (!value) {
        output << "null";
        return;
    }
    output << "{\"model_load_duration_ns\":";
    write_optional_duration(output, value->model_load_duration);
    output << ",\"prompt_evaluation_duration_ns\":";
    write_optional_duration(output, value->prompt_evaluation_duration);
    output << ",\"generation_duration_ns\":";
    write_optional_duration(output, value->generation_duration);
    output << ",\"input_token_count\":";
    write_optional_number(output, value->input_token_count);
    output << ",\"output_token_count\":";
    write_optional_number(output, value->output_token_count);
    output << '}';
}

void write_provider_identity(std::ostream& output,
                             const std::optional<ProviderIdentity>& value) {
    if (!value) {
        output << "null";
        return;
    }
    output << "{\"provider\":" << json_string(value->provider)
           << ",\"model\":" << json_string(value->model)
           << ",\"model_digest\":";
    if (value->model_digest) output << json_string(*value->model_digest);
    else output << "null";
    output << '}';
}

void write_diagnostic(std::ostream& output,
                      const ValidationDiagnostic& diagnostic) {
    output << "{\"category\":"
           << json_string(diagnostic_category_name(diagnostic.category))
           << ",\"code\":" << json_string(diagnostic.code)
           << ",\"message\":" << json_string(diagnostic.message)
           << ",\"range\":{\"start\":{\"line\":"
           << diagnostic.span.begin.line << ",\"column\":"
           << diagnostic.span.begin.column << ",\"offset\":"
           << diagnostic.span.begin.offset << "},\"end\":{\"line\":"
           << diagnostic.span.end.line << ",\"column\":"
           << diagnostic.span.end.column << ",\"offset\":"
           << diagnostic.span.end.offset << "}}}";
}

void write_requirement_verification(
    std::ostream& output, const RequirementVerificationResult& verification) {
    output << "{\"status\":"
           << json_string(requirement_status_name(verification.status))
           << ",\"outcomes\":[";
    for (std::size_t index = 0; index < verification.outcomes.size(); ++index) {
        if (index != 0) output << ',';
        const auto& item = verification.outcomes[index];
        output << "{\"requirement_id\":" << json_string(item.requirement_id)
               << ",\"status\":" << json_string(requirement_outcome_name(item.status))
               << ",\"code\":" << json_string(item.code)
               << ",\"expected\":" << json_string(item.expected)
               << ",\"actual\":" << json_string(item.actual)
               << ",\"explanation\":" << json_string(item.explanation) << '}';
    }
    output << "]}";
}

void write_support_assessment(std::ostream& output,
                              const SupportAssessment& assessment) {
    output << "{\"status\":" << json_string(support_status_name(assessment.status))
           << ",\"clauses\":[";
    for (std::size_t index = 0; index < assessment.clauses.size(); ++index) {
        if (index != 0) output << ',';
        const auto& clause = assessment.clauses[index];
        output << "{\"operation\":" << json_string(clause.operation)
               << ",\"status\":" << json_string(support_clause_status_name(clause.status))
               << ",\"code\":" << json_string(clause.code)
               << ",\"explanation\":" << json_string(clause.explanation)
               << ",\"source_begin\":" << clause.source_begin
               << ",\"source_end\":" << clause.source_end
               << ",\"source_text\":" << json_string(clause.source_text) << '}';
    }
    output << "]}";
}

LatencyStatistics latency_statistics(
    std::vector<std::chrono::milliseconds> latencies) {
    LatencyStatistics result;
    result.sample_count = latencies.size();
    std::ranges::sort(latencies);
    if (latencies.empty()) return result;
    const auto middle = latencies.size() / 2;
    if (latencies.size() % 2 == 0) {
        result.median = std::chrono::milliseconds{
            (latencies[middle - 1].count() + latencies[middle].count()) / 2};
    } else {
        result.median = latencies[middle];
    }
    const auto p90_index = static_cast<std::size_t>(
        std::ceil(static_cast<double>(latencies.size()) * 0.9) - 1.0);
    result.p90 = latencies[p90_index];
    return result;
}

ModelEvaluationSummary summarize(std::string model,
                                 const std::vector<CaseEvaluation>& cases) {
    ModelEvaluationSummary result;
    result.model = std::move(model);
    std::vector<std::chrono::milliseconds> latencies;
    std::vector<std::chrono::milliseconds> cold_latencies;
    std::vector<std::chrono::milliseconds> warm_latencies;
    std::size_t total_repairs{};
    double output_tokens{};
    double generation_seconds{};
    std::set<std::string, std::less<>> distinct_cases;
    std::map<std::string, std::pair<std::size_t, std::size_t>, std::less<>> case_success;
    std::map<std::size_t, std::pair<std::size_t, std::size_t>> repetition_success;
    std::size_t runs_with_repair{};
    std::size_t runs_with_requirement_correction{};
    for (const auto& item : cases) {
        if (item.model != result.model) continue;
        distinct_cases.insert(item.case_id);
        result.repetition_count = std::max(result.repetition_count, item.repetition);
        ++result.total_cases;
        result.applicable_cases += item.expected_supported ? 1U : 0U;
        switch (item.expected_support) {
        case SupportAssessmentStatus::Supported: ++result.supported_cases; break;
        case SupportAssessmentStatus::PartiallySupported:
            ++result.partially_supported_cases;
            break;
        case SupportAssessmentStatus::Unsupported: ++result.unsupported_cases; break;
        case SupportAssessmentStatus::Uncertain: ++result.uncertain_cases; break;
        }
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
        result.requirement_satisfied +=
            item.requirement_verification.status == RequirementSatisfactionStatus::Satisfied ? 1U : 0U;
        result.requirement_violated +=
            item.requirement_verification.status == RequirementSatisfactionStatus::Violated ? 1U : 0U;
        result.requirement_unknown +=
            item.requirement_verification.status == RequirementSatisfactionStatus::Unknown ? 1U : 0U;
        result.requirement_not_evaluated +=
            item.requirement_verification.status == RequirementSatisfactionStatus::NotEvaluated ? 1U : 0U;
        result.unsupported_recognized += item.unsupported_recognized ? 1U : 0U;
        result.support_assessment_matches += item.support_assessment_matches ? 1U : 0U;
        runs_with_repair += item.repair_attempts != 0 ? 1U : 0U;
        bool corrected_requirements = false;
        for (const auto& attempt : item.attempts) {
            corrected_requirements = corrected_requirements ||
                attempt.purpose == EvaluationCallPurpose::RequirementCorrection;
        }
        runs_with_requirement_correction += corrected_requirements ? 1U : 0U;
        if (item.expected_supported) {
            auto& by_case = case_success[item.case_id];
            ++by_case.second;
            auto& by_repetition = repetition_success[item.repetition];
            ++by_repetition.second;
            if (item.requirement_verification.status ==
                RequirementSatisfactionStatus::Satisfied) {
                ++by_case.first;
                ++by_repetition.first;
            }
        }
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
        if (item.warm_cold_status == WarmColdStatus::Cold) {
            cold_latencies.push_back(item.total_latency);
        } else if (item.warm_cold_status == WarmColdStatus::Warm) {
            warm_latencies.push_back(item.total_latency);
        }
        for (const auto& attempt : item.attempts) {
            result.output_limit_errors +=
                attempt.provider_error_code == ProviderErrorCode::OutputLimit ? 1U : 0U;
        }
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
        result.repair_rate = static_cast<double>(runs_with_repair) /
                             static_cast<double>(result.total_cases);
        result.requirement_correction_rate =
            static_cast<double>(runs_with_requirement_correction) /
            static_cast<double>(result.total_cases);
    }
    result.distinct_cases = distinct_cases.size();
    std::vector<double> case_rates;
    for (const auto& [unused, counts] : case_success) {
        static_cast<void>(unused);
        case_rates.push_back(static_cast<double>(counts.first) /
                             static_cast<double>(counts.second));
    }
    std::ranges::sort(case_rates);
    if (!case_rates.empty()) {
        const auto middle = case_rates.size() / 2;
        result.median_case_requirement_success_rate = case_rates.size() % 2 == 0
            ? (case_rates[middle - 1] + case_rates[middle]) / 2.0
            : case_rates[middle];
    }
    std::vector<double> repetition_rates;
    for (const auto& [unused, counts] : repetition_success) {
        static_cast<void>(unused);
        repetition_rates.push_back(static_cast<double>(counts.first) /
                                   static_cast<double>(counts.second));
    }
    if (!repetition_rates.empty()) {
        const auto mean = std::accumulate(repetition_rates.begin(),
                                          repetition_rates.end(), 0.0) /
                          static_cast<double>(repetition_rates.size());
        double squared_difference_sum{};
        for (const auto rate : repetition_rates) {
            squared_difference_sum += (rate - mean) * (rate - mean);
        }
        result.repetition_success_rate_standard_deviation =
            std::sqrt(squared_difference_sum /
                      static_cast<double>(repetition_rates.size()));
    }
    const auto aggregate_latency = latency_statistics(std::move(latencies));
    result.median_latency = aggregate_latency.median;
    result.p90_latency = aggregate_latency.p90;
    result.cold_latency = latency_statistics(std::move(cold_latencies));
    result.warm_latency = latency_statistics(std::move(warm_latencies));
    if (generation_seconds > 0.0) {
        result.average_output_tokens_per_second = output_tokens / generation_seconds;
    }
    std::ranges::sort(result.failure_categories, {},
                      &std::pair<std::string, std::size_t>::first);
    return result;
}

std::vector<CaseEvaluationTrend> build_case_trends(
    const std::vector<CaseEvaluation>& cases) {
    std::vector<CaseEvaluationTrend> result;
    std::map<std::pair<std::string, std::string>, std::size_t> indexes;
    std::vector<std::vector<std::chrono::milliseconds>> latencies;
    for (const auto& item : cases) {
        const auto key = std::pair{item.model, item.case_id};
        auto found = indexes.find(key);
        if (found == indexes.end()) {
            found = indexes.emplace(key, result.size()).first;
            result.push_back({item.case_id, item.category, item.model,
                              item.expected_support});
            latencies.emplace_back();
        }
        auto& trend = result[found->second];
        ++trend.repetitions;
        trend.final_valid += item.final_valid ? 1U : 0U;
        trend.requirement_satisfied +=
            item.requirement_verification.status ==
                    RequirementSatisfactionStatus::Satisfied
                ? 1U : 0U;
        trend.semantic_matches +=
            item.semantic_status == SemanticComparisonStatus::Match ? 1U : 0U;
        trend.support_assessment_matches += item.support_assessment_matches ? 1U : 0U;
        trend.runs_with_repair += item.repair_attempts != 0 ? 1U : 0U;
        trend.runs_with_requirement_correction += std::ranges::any_of(
            item.attempts, [](const EvaluationAttempt& attempt) {
                return attempt.purpose == EvaluationCallPurpose::RequirementCorrection;
            }) ? 1U : 0U;
        latencies[found->second].push_back(item.total_latency);
    }
    for (std::size_t index = 0; index < result.size(); ++index) {
        auto& trend = result[index];
        trend.persistent_requirement_failure =
            (trend.expected_support == SupportAssessmentStatus::Supported ||
             trend.expected_support == SupportAssessmentStatus::PartiallySupported) &&
            trend.requirement_satisfied == 0;
        trend.latency = latency_statistics(std::move(latencies[index]));
    }
    return result;
}

} // namespace

EvaluationReport evaluate_golden_dataset(const GoldenDataset& dataset,
                                         LlmProvider& provider,
                                         const EvaluationSettings& settings,
                                         CancellationToken cancellation,
                                         EvaluationClock clock,
                                         EvaluationProgressCallback progress) {
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
    if (settings.prompt_configuration_id.empty()) {
        throw std::invalid_argument("prompt configuration ID must not be empty");
    }
    const auto prompt_configuration = parse_prompt_configuration(
        settings.prompt_configuration_id);
    if (!prompt_configuration) {
        throw std::invalid_argument("unknown prompt configuration ID: " +
                                    settings.prompt_configuration_id);
    }
    if (*prompt_configuration == PromptConfiguration::StructuralRag) {
        throw std::invalid_argument(
            "structural-rag-v1 evaluation integration is not available until M6Q.8 Slice 4");
    }
    if (settings.comparison_version.empty()) {
        throw std::invalid_argument("comparison version must not be empty");
    }

    std::set<std::string, std::less<>> distinct_models;
    for (const auto& model : settings.models) {
        if (model.empty()) throw std::invalid_argument("model identifiers must not be empty");
        if (!distinct_models.insert(model).second) {
            throw std::invalid_argument("model identifiers must be unique");
        }
    }

    EvaluationReport report;
    report.prompt_configuration_id = settings.prompt_configuration_id;
    report.comparison_version = settings.comparison_version;
    report.requirement_schema_version = "golden-requirements-v2";
    report.scoring_version = "evaluation-trends-v1";
    report.dataset_version = dataset.dataset_version;
    report.timestamp_utc = settings.timestamp_utc;
    report.git_revision = settings.git_revision;
    report.temperature = settings.temperature;
    report.max_output_tokens = settings.max_output_tokens;
    report.timeout = settings.timeout;
    report.maximum_repair_attempts = settings.maximum_repair_attempts;
    const auto total_runs = settings.models.size() * settings.repetitions *
                            dataset.cases.size();
    std::size_t run_order{};
    for (const auto& model : settings.models) {
        bool first_model_run = true;
        for (std::size_t repetition = 1; repetition <= settings.repetitions; ++repetition) {
            for (const auto& golden : dataset.cases) {
                CaseEvaluation item;
                item.case_id = golden.metadata.id;
                item.category = golden.metadata.category;
                item.model = model;
                item.repetition = repetition;
                item.run_order = ++run_order;
                item.warm_cold_status = first_model_run ? WarmColdStatus::Cold
                                                        : WarmColdStatus::Warm;
                first_model_run = false;
                item.expected_supported = golden.metadata.expected_valid;
                item.expected_support = golden.metadata.expected_support;
                if (progress) {
                    progress(EvaluationProgressStage::Started, item,
                             run_order - 1, total_runs);
                }

                ObservingProvider observed{provider, clock};
                PromptBuilder prompts{{model, settings.temperature,
                                       settings.max_output_tokens, settings.timeout,
                                       *prompt_configuration}};
                AgentSession session{observed, std::move(prompts),
                                     {settings.maximum_repair_attempts}};
                const auto started = clock();
                const AgentResult agent_result = session.generate(
                    {NaturalLanguageInstruction{golden.prompt}}, cancellation);
                const auto finished = clock();
                item.total_latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                    finished - started);
                observed.finalize(golden.expected_source,
                                  golden.metadata.expected_valid,
                                  golden.metadata.requirements);
                item.provider_identity = std::move(observed.identity);
                item.attempts = std::move(observed.attempts);
                item.provider_attempts.reserve(item.attempts.size());
                for (const auto& attempt : item.attempts) {
                    item.provider_attempts.push_back(attempt.provider_metrics);
                }
                item.repair_attempts = item.attempts.empty()
                                           ? 0
                                           : item.attempts.size() - 1;
                item.first_attempt_valid = !item.attempts.empty() &&
                                           item.attempts.front().compiler_valid == true;

                const ScriptProposal* proposal = std::get_if<ScriptProposal>(&agent_result);
                if (proposal != nullptr) {
                    item.generation_status = EvaluationGenerationStatus::Completed;
                    item.support_assessment = proposal->support_assessment();
                } else if (const auto* unsupported =
                               std::get_if<UnsupportedRequest>(&agent_result)) {
                    item.generation_status = EvaluationGenerationStatus::Unsupported;
                    item.support_assessment = unsupported->assessment;
                    item.unsupported_recognized =
                        item.expected_support == SupportAssessmentStatus::Unsupported;
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
                    item.final_warnings = proposal->warnings();
                    const auto validation = validate_script(*item.generated_source);
                    item.final_valid = validation.ok();
                    if (golden.metadata.expected_valid && item.final_valid) {
                        item.semantic_status = compare_script_semantics(
                                                   golden.expected_source,
                                                   *item.generated_source)
                                                   .status;
                        const auto facts = compile_program_facts(*item.generated_source);
                        if (facts) {
                            item.requirement_verification = verify_requirements(
                                golden.metadata.requirements, *facts);
                        }
                    }
                }
                item.support_assessment_matches =
                    item.generation_status != EvaluationGenerationStatus::Failed &&
                    item.generation_status != EvaluationGenerationStatus::Cancelled &&
                    item.support_assessment.status == item.expected_support;
                report.cases.push_back(std::move(item));
                if (progress) {
                    progress(EvaluationProgressStage::Completed,
                             report.cases.back(), run_order, total_runs);
                }
                if (cancellation.stop_requested()) break;
            }
            if (cancellation.stop_requested()) break;
        }
        if (cancellation.stop_requested()) break;
    }
    report.case_trends = build_case_trends(report.cases);
    for (const auto& model : settings.models) {
        report.summaries.push_back(summarize(model, report.cases));
    }
    return report;
}

void write_evaluation_json(std::ostream& output, const EvaluationReport& report) {
    output << "{\"schema\":\"horusrf.ai-evaluation\",\"version\":2"
           << ",\"dataset_version\":" << json_string(report.dataset_version)
           << ",\"comparison_version\":" << json_string(report.comparison_version)
           << ",\"requirement_schema_version\":"
           << json_string(report.requirement_schema_version)
           << ",\"scoring_version\":" << json_string(report.scoring_version)
           << ",\"prompt_configuration_id\":"
           << json_string(report.prompt_configuration_id)
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
               << ",\"run_order\":" << item.run_order
               << ",\"warm_cold_status\":"
               << json_string(warm_cold_name(item.warm_cold_status))
               << ",\"expected_supported\":" << (item.expected_supported ? "true" : "false")
               << ",\"expected_support\":"
               << json_string(support_status_name(item.expected_support))
               << ",\"generation_status\":" << json_string(generation_name(item.generation_status))
               << ",\"first_attempt_valid\":" << (item.first_attempt_valid ? "true" : "false")
               << ",\"final_valid\":" << (item.final_valid ? "true" : "false")
               << ",\"semantic_status\":" << json_string(semantic_name(item.semantic_status))
               << ",\"requirement_verification\":";
        write_requirement_verification(output, item.requirement_verification);
        output << ",\"support_assessment\":";
        write_support_assessment(output, item.support_assessment);
        output << ",\"unsupported_recognized\":" << (item.unsupported_recognized ? "true" : "false")
               << ",\"support_assessment_matches\":"
               << (item.support_assessment_matches ? "true" : "false")
               << ",\"repair_attempts\":" << item.repair_attempts
               << ",\"total_latency_ms\":" << item.total_latency.count()
               << ",\"provider_identity\":";
        write_provider_identity(output, item.provider_identity);
        output
               << ",\"provider_attempts\":[";
        for (std::size_t attempt = 0; attempt < item.provider_attempts.size(); ++attempt) {
            if (attempt != 0) output << ',';
            write_provider_metrics(output, item.provider_attempts[attempt]);
        }
        output << "],\"attempts\":[";
        for (std::size_t attempt_index = 0;
             attempt_index < item.attempts.size(); ++attempt_index) {
            if (attempt_index != 0) output << ',';
            const auto& attempt = item.attempts[attempt_index];
            output << "{\"number\":" << attempt.number
                   << ",\"purpose\":" << json_string(call_purpose_name(attempt.purpose))
                   << ",\"prompt_size\":{\"total_utf8_bytes\":"
                   << attempt.prompt_size.total_utf8_bytes << ",\"sections\":[";
            for (std::size_t section = 0;
                 section < attempt.prompt_size.sections.size(); ++section) {
                if (section != 0) output << ',';
                output << "{\"section\":"
                       << json_string(attempt.prompt_size.sections[section].section)
                       << ",\"utf8_bytes\":"
                       << attempt.prompt_size.sections[section].utf8_bytes << '}';
            }
            output << "]},\"structured_response_status\":"
                   << json_string(structured_response_name(
                          attempt.structured_response_status))
                   << ",\"provider_error_code\":";
            if (attempt.provider_error_code) {
                output << json_string(provider_error_name(*attempt.provider_error_code));
            } else {
                output << "null";
            }
            output
                   << ",\"response_error\":";
            if (attempt.response_error) output << json_string(*attempt.response_error);
            else output << "null";
            output << ",\"candidate_source\":";
            if (attempt.candidate_source) output << json_string(*attempt.candidate_source);
            else output << "null";
            output << ",\"candidate_hash\":";
            if (attempt.candidate_hash) output << json_string(*attempt.candidate_hash);
            else output << "null";
            output << ",\"repeated_candidate\":"
                   << (attempt.repeated_candidate ? "true" : "false")
                   << ",\"diagnostic_set_changed\":";
            if (attempt.diagnostic_set_changed) {
                output << (*attempt.diagnostic_set_changed ? "true" : "false");
            } else {
                output << "null";
            }
            output << ",\"explanation\":";
            if (attempt.explanation) output << json_string(*attempt.explanation);
            else output << "null";
            output << ",\"warnings\":[";
            for (std::size_t warning = 0; warning < attempt.warnings.size(); ++warning) {
                if (warning != 0) output << ',';
                output << json_string(attempt.warnings[warning]);
            }
            output << "],\"compiler_valid\":";
            if (attempt.compiler_valid) {
                output << (*attempt.compiler_valid ? "true" : "false");
            } else output << "null";
            output << ",\"compiler_diagnostics\":[";
            for (std::size_t diagnostic = 0;
                 diagnostic < attempt.compiler_diagnostics.size(); ++diagnostic) {
                if (diagnostic != 0) output << ',';
                write_diagnostic(output, attempt.compiler_diagnostics[diagnostic]);
            }
            output << "],\"semantic_status\":"
                   << json_string(semantic_name(attempt.semantic_status))
                   << ",\"semantic_differences\":[";
            for (std::size_t difference = 0;
                 difference < attempt.semantic_differences.size(); ++difference) {
                if (difference != 0) output << ',';
                const auto& value = attempt.semantic_differences[difference];
                output << "{\"path\":" << json_string(value.path)
                       << ",\"expected\":" << json_string(value.expected)
                       << ",\"actual\":" << json_string(value.actual) << '}';
            }
            output << "],\"requirement_verification\":";
            write_requirement_verification(output, attempt.requirement_verification);
            output << ",\"compiler_duration_ns\":";
            write_optional_duration(output, attempt.compiler_duration);
            output << ",\"verification_duration_ns\":";
            write_optional_duration(output, attempt.verification_duration);
            output << ",\"provider_metrics\":";
            write_provider_metrics(output, attempt.provider_metrics);
            output << ",\"provider_identity\":";
            write_provider_identity(output, attempt.provider_identity);
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
        output << ",\"final_warnings\":[";
        for (std::size_t warning = 0; warning < item.final_warnings.size(); ++warning) {
            if (warning != 0) output << ',';
            output << json_string(item.final_warnings[warning]);
        }
        output << ']';
        output << '}';
    }
    output << "],\"case_trends\":[";
    for (std::size_t index = 0; index < report.case_trends.size(); ++index) {
        if (index != 0) output << ',';
        const auto& item = report.case_trends[index];
        output << "{\"case_id\":" << json_string(item.case_id)
               << ",\"category\":" << json_string(item.category)
               << ",\"model\":" << json_string(item.model)
               << ",\"expected_support\":"
               << json_string(support_status_name(item.expected_support))
               << ",\"repetitions\":" << item.repetitions
               << ",\"final_valid\":" << item.final_valid
               << ",\"requirement_satisfied\":" << item.requirement_satisfied
               << ",\"semantic_matches\":" << item.semantic_matches
               << ",\"support_assessment_matches\":"
               << item.support_assessment_matches
               << ",\"runs_with_repair\":" << item.runs_with_repair
               << ",\"runs_with_requirement_correction\":"
               << item.runs_with_requirement_correction
               << ",\"persistent_requirement_failure\":"
               << (item.persistent_requirement_failure ? "true" : "false")
               << ",\"latency\":{\"sample_count\":" << item.latency.sample_count
               << ",\"median_ms\":" << item.latency.median.count()
               << ",\"p90_ms\":" << item.latency.p90.count() << "}}";
    }
    output << "],\"summaries\":[";
    for (std::size_t index = 0; index < report.summaries.size(); ++index) {
        if (index != 0) output << ',';
        const auto& item = report.summaries[index];
        output << "{\"model\":" << json_string(item.model)
               << ",\"total_cases\":" << item.total_cases
               << ",\"applicable_cases\":" << item.applicable_cases
               << ",\"supported_cases\":" << item.supported_cases
               << ",\"partially_supported_cases\":"
               << item.partially_supported_cases
               << ",\"unsupported_cases\":" << item.unsupported_cases
               << ",\"uncertain_cases\":" << item.uncertain_cases
               << ",\"generation_completed\":" << item.generation_completed
               << ",\"first_attempt_valid\":" << item.first_attempt_valid
               << ",\"final_valid\":" << item.final_valid
               << ",\"semantic_matches\":" << item.semantic_matches
               << ",\"semantic_mismatches\":" << item.semantic_mismatches
               << ",\"semantic_not_evaluated\":" << item.semantic_not_evaluated
               << ",\"requirement_satisfied\":" << item.requirement_satisfied
               << ",\"requirement_violated\":" << item.requirement_violated
               << ",\"requirement_unknown\":" << item.requirement_unknown
               << ",\"requirement_not_evaluated\":" << item.requirement_not_evaluated
               << ",\"unsupported_recognized\":" << item.unsupported_recognized
               << ",\"support_assessment_matches\":" << item.support_assessment_matches
               << ",\"distinct_cases\":" << item.distinct_cases
               << ",\"repetition_count\":" << item.repetition_count
               << ",\"median_case_requirement_success_rate\":"
               << item.median_case_requirement_success_rate
               << ",\"repetition_success_rate_standard_deviation\":"
               << item.repetition_success_rate_standard_deviation
               << ",\"repair_rate\":" << item.repair_rate
               << ",\"requirement_correction_rate\":"
               << item.requirement_correction_rate
               << ",\"average_repair_attempts\":" << item.average_repair_attempts
               << ",\"median_latency_ms\":" << item.median_latency.count()
               << ",\"p90_latency_ms\":" << item.p90_latency.count()
               << ",\"cold_latency\":{\"sample_count\":"
               << item.cold_latency.sample_count << ",\"median_ms\":"
               << item.cold_latency.median.count() << ",\"p90_ms\":"
               << item.cold_latency.p90.count() << "}"
               << ",\"warm_latency\":{\"sample_count\":"
               << item.warm_latency.sample_count << ",\"median_ms\":"
               << item.warm_latency.median.count() << ",\"p90_ms\":"
               << item.warm_latency.p90.count() << "}"
               << ",\"output_limit_errors\":" << item.output_limit_errors
               << ",\"average_output_tokens_per_second\":";
        write_optional_number(output, item.average_output_tokens_per_second);
        output << ",\"rates\":{\"generation_completed\":{\"numerator\":"
               << item.generation_completed << ",\"denominator\":" << item.total_cases
               << "},\"first_attempt_valid\":{\"numerator\":"
               << item.first_attempt_valid << ",\"denominator\":" << item.total_cases
               << "},\"final_valid\":{\"numerator\":" << item.final_valid
               << ",\"denominator\":" << item.total_cases
               << "},\"requirement_satisfied\":{\"numerator\":"
               << item.requirement_satisfied << ",\"denominator\":"
               << item.applicable_cases
               << "},\"semantic_matches\":{\"numerator\":" << item.semantic_matches
               << ",\"denominator\":" << item.applicable_cases
               << "},\"unsupported_recognized\":{\"numerator\":"
               << item.unsupported_recognized << ",\"denominator\":"
               << item.unsupported_cases
               << "},\"support_assessment_matches\":{\"numerator\":"
               << item.support_assessment_matches << ",\"denominator\":"
               << item.total_cases << "}},\"failure_categories\":{";
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
    output << "Prompt configuration: " << report.prompt_configuration_id << '\n';
    output << "Model                       Cases  First valid  Final valid  Required  Semantic  Cold ms  Warm median  Warm P90  Output limit\n";
    output << "-----------------------------------------------------------------------------------------------------------------------\n";
    for (const auto& item : report.summaries) {
        output << std::left << std::setw(27) << item.model << std::right
               << std::setw(6) << item.total_cases << std::setw(13)
               << item.first_attempt_valid << std::setw(13) << item.final_valid
               << std::setw(10) << item.requirement_satisfied
               << std::setw(10) << item.semantic_matches << std::setw(9)
               << item.cold_latency.median.count() << std::setw(13)
               << item.warm_latency.median.count() << std::setw(10)
               << item.warm_latency.p90.count() << std::setw(14)
               << item.output_limit_errors << '\n';
    }
    output << "Required is explicit requirement satisfaction; Semantic is strict full-program comparison.\n";
    if (!report.case_trends.empty()) {
        output << "\nPer-case persistence (successes/repetitions):\n";
        output << "Model                       Case                                      Required  Support  Repair  Correct  Median ms  P90 ms\n";
        output << "-----------------------------------------------------------------------------------------------------------------------------\n";
        for (const auto& item : report.case_trends) {
            output << std::left << std::setw(27) << item.model
                   << std::setw(42) << item.case_id << std::right
                   << std::setw(4) << item.requirement_satisfied << '/'
                   << std::left << std::setw(5) << item.repetitions << std::right
                   << std::setw(4) << item.support_assessment_matches << '/'
                   << std::left << std::setw(5) << item.repetitions << std::right
                   << std::setw(7) << item.runs_with_repair
                   << std::setw(9) << item.runs_with_requirement_correction
                   << std::setw(11) << item.latency.median.count()
                   << std::setw(8) << item.latency.p90.count() << '\n';
        }
    }
}

} // namespace horusrf::ai
