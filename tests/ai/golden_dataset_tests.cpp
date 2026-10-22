#include "test_support.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "horusrf/ai/golden_dataset.hpp"
#include "horusrf/ai/support_assessment.hpp"

namespace {

namespace fs = std::filesystem;
using namespace horusrf;

const fs::path fixture_root = fs::path{HORUSRF_BINARY_DIR} / "golden-dataset-fixtures";

struct FixtureCleanup final {
    ~FixtureCleanup() { std::filesystem::remove_all(fixture_root); }
};

void write(const fs::path& path, std::string_view content) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << content;
    if (!output) throw std::runtime_error("failed to write fixture " + path.string());
}

std::string manifest(std::string_view cases) {
    return std::string{"{\n  \"schema\": \"horusrf.golden-dataset\",\n"}
        + "  \"version\": 1,\n  \"dataset_version\": \"test\",\n"
        + "  \"cases\": [" + std::string{cases} + "]\n}\n";
}

std::string expanded_manifest(std::string_view cases,
                              std::size_t baseline_case_count = 1) {
    return std::string{"{\n  \"schema\": \"horusrf.golden-dataset\",\n"}
        + "  \"version\": 2,\n  \"dataset_version\": \"2.0.0\",\n"
        + "  \"baseline_dataset_version\": \"1.0.0\",\n"
        + "  \"baseline_case_count\": " + std::to_string(baseline_case_count)
        + ",\n  \"cases\": [" + std::string{cases} + "]\n}\n";
}

std::string metadata(std::string_view id) {
    return std::string{"{\n  \"id\": \""} + std::string{id} +
        "\",\n  \"description\": \"Test case\",\n"
        "  \"category\": \"frequency_sweep\",\n"
        "  \"difficulty\": \"basic\",\n  \"tags\": [\"frequency\"],\n"
        "  \"expected_valid\": true,\n  \"comparison\": \"semantic\",\n"
        "  \"required_capabilities\": [\"frequency_sweep\"]\n}\n";
}

constexpr std::string_view valid_source = R"(characterize fixture {
  reference power = -10 dBm
  sweep frequency 1 GHz .. 2 GHz step 1 GHz
  measure power
  derive calibration cal { correction = reference.power - power over frequency }
}
)";

void add_case(const fs::path& root, std::string_view directory, std::string_view id,
              std::string_view source = valid_source) {
    const auto path = root / "cases" / directory;
    write(path / "prompt.txt", "Characterize a basic frequency sweep.\n");
    write(path / "expected.hrf", source);
    write(path / "metadata.json", metadata(id));
}

bool has_message(const ai::GoldenDatasetResult& result, std::string_view part) {
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.message.find(part) != std::string::npos) return true;
    }
    return false;
}

void checked_in_dataset_test() {
    const auto result = ai::load_golden_dataset(
        fs::path{HORUSRF_SOURCE_DIR} / "tests" / "ai" / "golden");
    if (!result.ok()) {
        throw std::runtime_error(result.diagnostics.front().path.string() + ": " +
                                 result.diagnostics.front().message);
    }
    CHECK_EQ(result.dataset->dataset_version, "2.0.0");
    CHECK_EQ(result.dataset->baseline_dataset_version,
             std::optional<std::string>{"1.0.0"});
    CHECK_EQ(result.dataset->baseline_case_count, std::size_t{17});
    CHECK_EQ(result.dataset->cases.size(), std::size_t{30});
    std::size_t supported{};
    std::size_t unsupported{};
    std::size_t version_two{};
    std::size_t version_three{};
    std::size_t partially_supported{};
    std::size_t uncertain{};
    for (const auto& test_case : result.dataset->cases) {
        CHECK(!test_case.prompt.empty());
        CHECK(!test_case.expected_source.empty());
        version_two += test_case.metadata.requirement_schema_version == 2 ? 1U : 0U;
        version_three += test_case.metadata.requirement_schema_version == 3 ? 1U : 0U;
        partially_supported += test_case.metadata.expected_support ==
                                   ai::SupportAssessmentStatus::PartiallySupported
                                   ? 1U : 0U;
        uncertain += test_case.metadata.expected_support ==
                         ai::SupportAssessmentStatus::Uncertain
                         ? 1U : 0U;
        if (test_case.metadata.expected_valid) {
            ++supported;
            CHECK(!test_case.metadata.requirements.empty());
            const auto facts = ai::compile_program_facts(test_case.expected_source);
            CHECK(facts.has_value());
            CHECK_EQ(ai::verify_requirements(test_case.metadata.requirements, *facts).status,
                     ai::RequirementSatisfactionStatus::Satisfied);
        } else {
            ++unsupported;
            CHECK(test_case.metadata.requirements.empty());
        }
        if (test_case.metadata.id == "mixed_supported_unsupported_sweeps" ||
            test_case.metadata.id == "ambiguous_power_range") {
            CHECK_EQ(ai::assess_instruction_support(test_case.prompt).status,
                     test_case.metadata.expected_support);
        }
    }
    CHECK_EQ(supported, std::size_t{28});
    CHECK_EQ(unsupported, std::size_t{2});
    CHECK_EQ(version_two, std::size_t{17});
    CHECK_EQ(version_three, std::size_t{13});
    CHECK_EQ(partially_supported, std::size_t{1});
    CHECK_EQ(uncertain, std::size_t{1});
}

void malformed_metadata_test() {
    const auto root = fixture_root / "malformed";
    write(root / "manifest.json", manifest("\"case_one\""));
    add_case(root, "case_one", "case_one");
    write(root / "cases" / "case_one" / "metadata.json", "{\"id\":\"case_one\"}\n");
    const auto result = ai::load_golden_dataset(root);
    CHECK(!result.ok());
    CHECK(has_message(result, "missing required member"));
}

void duplicate_identifier_test() {
    const auto root = fixture_root / "duplicate";
    write(root / "manifest.json", manifest("\"case_one\", \"case_two\""));
    add_case(root, "case_one", "shared");
    add_case(root, "case_two", "shared");
    const auto result = ai::load_golden_dataset(root);
    CHECK(!result.ok());
    CHECK(has_message(result, "duplicate case id 'shared'"));
}

void invalid_expected_script_test() {
    const auto root = fixture_root / "invalid-script";
    write(root / "manifest.json", manifest("\"case_one\""));
    add_case(root, "case_one", "case_one", "characterize broken { measure @ }\n");
    const auto result = ai::load_golden_dataset(root);
    CHECK(!result.ok());
    CHECK(has_message(result, "expected-valid script failed compiler validation"));
}

void unlisted_directory_test() {
    const auto root = fixture_root / "unlisted";
    write(root / "manifest.json", manifest("\"case_one\""));
    add_case(root, "case_one", "case_one");
    add_case(root, "forgotten", "forgotten");
    const auto result = ai::load_golden_dataset(root);
    CHECK(!result.ok());
    CHECK(has_message(result, "case directory is not listed in manifest"));
}

void manifest_migration_and_version_rejection_test() {
    const auto migrated = fixture_root / "manifest-v2";
    write(migrated / "manifest.json", expanded_manifest("\"case_one\""));
    add_case(migrated, "case_one", "case_one");
    const auto migrated_result = ai::load_golden_dataset(migrated);
    CHECK(migrated_result.ok());
    CHECK_EQ(migrated_result.dataset->baseline_dataset_version,
             std::optional<std::string>{"1.0.0"});
    CHECK_EQ(migrated_result.dataset->baseline_case_count, std::size_t{1});

    const auto future = fixture_root / "manifest-future";
    write(future / "manifest.json",
          "{\"schema\":\"horusrf.golden-dataset\",\"version\":99,"
          "\"dataset_version\":\"future\",\"cases\":[\"case_one\"]}\n");
    const auto future_result = ai::load_golden_dataset(future);
    CHECK(!future_result.ok());
    CHECK(has_message(future_result, "unsupported manifest version"));

    const auto invalid_count = fixture_root / "manifest-count";
    write(invalid_count / "manifest.json",
          expanded_manifest("\"case_one\"", 2));
    add_case(invalid_count, "case_one", "case_one");
    const auto invalid_count_result = ai::load_golden_dataset(invalid_count);
    CHECK(!invalid_count_result.ok());
    CHECK(has_message(invalid_count_result,
                      "baseline_case_count exceeds the number of manifest cases"));
}

} // namespace

int main() {
    try {
        FixtureCleanup cleanup;
        std::filesystem::remove_all(fixture_root);
        checked_in_dataset_test();
        malformed_metadata_test();
        duplicate_identifier_test();
        invalid_expected_script_test();
        unlisted_directory_test();
        manifest_migration_and_version_rejection_test();
        std::cout << "All HorusRF golden dataset tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
