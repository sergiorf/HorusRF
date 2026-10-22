#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "horusrf/ai/requirement_verification.hpp"

namespace horusrf::ai {

struct GoldenCaseMetadata final {
    std::string id;
    std::string description;
    std::string category;
    std::string difficulty;
    std::vector<std::string> tags;
    bool expected_valid{};
    std::string comparison;
    std::vector<std::string> required_capabilities;
    std::size_t requirement_schema_version{1};
    std::vector<RequirementClause> requirements;
    SupportAssessmentStatus expected_support{SupportAssessmentStatus::Supported};
};

struct GoldenCase final {
    GoldenCaseMetadata metadata;
    std::string prompt;
    std::string expected_source;
    std::filesystem::path directory;
};

struct GoldenDataset final {
    std::string dataset_version;
    std::vector<GoldenCase> cases;
    std::optional<std::string> baseline_dataset_version;
    std::size_t baseline_case_count{};
};

struct GoldenDatasetDiagnostic final {
    std::filesystem::path path;
    std::string message;
};

struct GoldenDatasetResult final {
    std::optional<GoldenDataset> dataset;
    std::vector<GoldenDatasetDiagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept {
        return dataset.has_value() && diagnostics.empty();
    }
};

// Loads and validates the golden dataset. Version-1 case metadata remains
// accepted for migration; version 2 adds curated typed requirements.
// It performs no model, network, runtime, simulator, or hardware operations.
[[nodiscard]] GoldenDatasetResult load_golden_dataset(
    const std::filesystem::path& root);

} // namespace horusrf::ai
