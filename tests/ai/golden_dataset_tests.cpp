#include "test_support.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "horusrf/ai/golden_dataset.hpp"

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
    CHECK_EQ(result.dataset->dataset_version, "1.0.0");
    CHECK_EQ(result.dataset->cases.size(), std::size_t{17});
    std::size_t supported{};
    std::size_t unsupported{};
    for (const auto& test_case : result.dataset->cases) {
        CHECK(!test_case.prompt.empty());
        CHECK(!test_case.expected_source.empty());
        CHECK_EQ(test_case.metadata.requirement_schema_version, std::size_t{2});
        if (test_case.metadata.expected_valid) {
            ++supported;
            CHECK(!test_case.metadata.requirements.empty());
        } else {
            ++unsupported;
            CHECK(test_case.metadata.requirements.empty());
        }
    }
    CHECK_EQ(supported, std::size_t{16});
    CHECK_EQ(unsupported, std::size_t{1});
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
        std::cout << "All HorusRF golden dataset tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
