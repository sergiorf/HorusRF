#pragma once

#include <filesystem>
#include <optional>
#include <string>
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
