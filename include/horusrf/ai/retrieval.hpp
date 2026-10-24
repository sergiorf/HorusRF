#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace horusrf::ai {

struct RetrievalDependency final {
    std::string producer;
    std::string consumer;
};

struct RetrievalProvenance final {
    std::string origin;
    std::string authoring_date;
    std::vector<std::string> influenced_by;
};

struct RetrievalExampleMetadata final {
    std::string id;
    std::string category;
    std::vector<std::string> operations;
    std::vector<std::string> structural_steps;
    std::vector<std::string> sweep_dimensions;
    std::vector<RetrievalDependency> dependencies;
    std::string comparison_strictness;
    std::string complexity;
    RetrievalProvenance provenance;
};

struct RetrievalExample final {
    RetrievalExampleMetadata metadata;
    std::string request;
    std::string program;
    std::filesystem::path directory;
};

enum class LeakageMatchKind {
    ExactRequest,
    LikelyRequestParaphrase,
    ExactProgram,
    LikelyProgramClone,
};

struct LeakageFinding final {
    std::string corpus_example_id;
    std::string comparison_id;
    LeakageMatchKind kind{LeakageMatchKind::ExactRequest};
    double similarity{};
};

struct LeakageReport final {
    std::string comparison_dataset_id;
    std::vector<LeakageFinding> findings;
};

struct RetrievalCorpus final {
    std::string corpus_id;
    std::string corpus_version;
    std::string content_digest;
    std::vector<RetrievalExample> examples;
    LeakageReport leakage_report;
};

struct RetrievalCorpusDiagnostic final {
    std::filesystem::path path;
    std::string message;
};

struct RetrievalCorpusResult final {
    std::optional<RetrievalCorpus> corpus;
    std::vector<RetrievalCorpusDiagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept {
        return corpus.has_value() && diagnostics.empty();
    }
};

enum class RetrievalComplexity {
    Basic,
    Intermediate,
    Advanced,
};

struct RetrievalQuery final {
    std::string instruction;
    std::vector<std::string> operations;
    std::vector<std::string> unsupported_operations;
    std::vector<std::string> sweep_dimensions;
    std::string comparison_strictness{"none"};
    RetrievalComplexity complexity{RetrievalComplexity::Basic};
    bool fixed_frequency{};
    bool named_values{};
    bool dependency_chain{};
    bool calibration{};
    std::size_t unknown_evidence_count{};
};

enum class RetrievalBudgetUnit {
    Utf8Bytes,
    EstimatedTokens,
};

struct RetrievalSettings final {
    std::size_t top_k{2};
    std::size_t context_budget{4096};
    RetrievalBudgetUnit budget_unit{RetrievalBudgetUnit::Utf8Bytes};
};

struct RetrievalComponentScores final {
    double structural{};
    double operation_coverage{};
    double typed_feature_coverage{};
    double lexical_similarity{};
    double complexity_compatibility{};
    double completeness_bonus{};
    double conflict_penalty{};
    double total{};
};

struct RetrievedExample final {
    std::string id;
    RetrievalComponentScores scores;
    std::string rationale;
    std::size_t context_bytes{};
    std::size_t estimated_tokens{};
};

enum class RetrievalDiagnosticCode {
    EmptyInstruction,
    InvalidTopK,
    EmptyCorpus,
    NoStructuralEvidence,
    NoMatchingExample,
};

struct RetrievalDiagnostic final {
    RetrievalDiagnosticCode code{RetrievalDiagnosticCode::NoMatchingExample};
    std::string message;
};

struct RetrievalResult final {
    std::string corpus_id;
    std::string corpus_version;
    std::string corpus_content_digest;
    std::string ranking_id;
    RetrievalQuery query;
    std::vector<RetrievedExample> selected;
    std::vector<std::string> rejected_by_budget;
    std::vector<std::string> suppressed_near_duplicates;
    std::vector<RetrievalDiagnostic> diagnostics;
    std::chrono::nanoseconds duration{};
    std::size_t inserted_context_bytes{};
    std::size_t inserted_context_estimated_tokens{};

    [[nodiscard]] bool ok() const noexcept {
        return diagnostics.empty() && !selected.empty();
    }
};

[[nodiscard]] std::string_view retrieval_ranking_id() noexcept;

// Conservatively projects compiler-recognized requirements and support clauses
// into retrieval features. Evidence not recognized with high confidence is
// retained only as unknown evidence and never broadened into an operation.
[[nodiscard]] RetrievalQuery analyze_retrieval_query(
    std::string_view instruction);

class ExampleRetriever final {
public:
    explicit ExampleRetriever(RetrievalCorpus corpus);

    [[nodiscard]] const RetrievalCorpus& corpus() const noexcept { return corpus_; }
    [[nodiscard]] RetrievalResult retrieve(
        std::string_view instruction,
        RetrievalSettings settings = {}) const;

private:
    RetrievalCorpus corpus_;
};

// Loads a local, versioned retrieval corpus and validates its exact schema,
// identity, file agreement, UTF-8, digest, and every program through the full
// compiler pipeline. When supplied, comparison_dataset_root is a golden dataset
// used only to produce deterministic leakage findings.
[[nodiscard]] RetrievalCorpusResult load_retrieval_corpus(
    const std::filesystem::path& root,
    const std::optional<std::filesystem::path>& comparison_dataset_root = std::nullopt);

[[nodiscard]] std::string retrieval_corpus_content_digest(
    const std::filesystem::path& root);

void write_leakage_report(const std::filesystem::path& path,
                          const RetrievalCorpus& corpus);

} // namespace horusrf::ai
