#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

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

// Loads and validates a version-1 golden dataset. This includes metadata and
// manifest validation plus complete compiler validation of every expected script.
// It performs no model, network, runtime, simulator, or hardware operations.
[[nodiscard]] GoldenDatasetResult load_golden_dataset(
    const std::filesystem::path& root);

} // namespace horusrf::ai
