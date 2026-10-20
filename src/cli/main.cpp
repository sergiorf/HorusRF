#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <locale>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "horusrf/ai/validation.hpp"
#include "horusrf/device/simulated_rf_device.hpp"
#include "horusrf/frontend/language_services.hpp"
#include "horusrf/ir/lowering.hpp"
#include "horusrf/output/csv.hpp"
#include "horusrf/output/json.hpp"
#include "horusrf/runtime/characterization.hpp"

namespace {
namespace fs = std::filesystem;
using namespace horusrf;

constexpr int usage_error = 2;
constexpr int io_error = 3;
constexpr int diagnostic_error = 4;
constexpr int unexpected_error = 5;
constexpr int qualification_failure = 6;
constexpr std::string_view usage =
    "usage: horusrf-run <source.hrf> [--check [--diagnostics=json] | --csv <output.csv> | --json <output.json>]";

struct Arguments {
    fs::path source;
    std::optional<fs::path> csv;
    std::optional<fs::path> json;
    bool check = false;
    bool structured_diagnostics = false;
};

int report_usage(std::string_view message) {
    std::cerr << usage << '\n' << "horusrf-run: " << message << '\n';
    return usage_error;
}

std::optional<Arguments> parse_arguments(int argc, char** argv, int& exit_code) {
    if (argc == 2 && std::string_view{argv[1]} == "--help") {
        std::cout << usage << '\n';
        exit_code = 0;
        return std::nullopt;
    }
    if (argc < 2) {
        exit_code = report_usage("a source path is required");
        return std::nullopt;
    }
    if (std::string_view{argv[1]}.starts_with("--")) {
        exit_code = report_usage("unknown option");
        return std::nullopt;
    }

    Arguments arguments{fs::path{argv[1]}, std::nullopt, std::nullopt, false};
    if (argc == 2) return arguments;
    if (argc == 3 && std::string_view{argv[2]} == "--check") {
        arguments.check = true;
        return arguments;
    }
    if (argc == 4 && std::string_view{argv[2]} == "--check" &&
        std::string_view{argv[3]} == "--diagnostics=json") {
        arguments.check = true;
        arguments.structured_diagnostics = true;
        return arguments;
    }
    if (argc != 4 ||
        (std::string_view{argv[2]} != "--csv" &&
         std::string_view{argv[2]} != "--json") ||
        std::string_view{argv[3]}.empty()) {
        exit_code = report_usage(
            "expected --check [--diagnostics=json], --csv, or --json with one output path");
        return std::nullopt;
    }
    if (std::string_view{argv[2]} == "--csv") arguments.csv = fs::path{argv[3]};
    else arguments.json = fs::path{argv[3]};
    return arguments;
}

bool same_path(const fs::path& left, const fs::path& right) {
    std::error_code left_error;
    std::error_code right_error;
    const auto normalized_left = fs::absolute(left, left_error).lexically_normal();
    const auto normalized_right = fs::absolute(right, right_error).lexically_normal();
    if (!left_error && !right_error && normalized_left == normalized_right) return true;

    std::error_code exists_left_error;
    std::error_code exists_right_error;
    const bool left_exists = fs::exists(left, exists_left_error);
    const bool right_exists = fs::exists(right, exists_right_error);
    if (!exists_left_error && !exists_right_error && left_exists && right_exists) {
        std::error_code equivalent_error;
        const bool equivalent = fs::equivalent(left, right, equivalent_error);
        return !equivalent_error && equivalent;
    }
    return false;
}

template <typename Diagnostics>
void print_frontend_diagnostics(const fs::path& source, const Diagnostics& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        const auto& position = diagnostic.span.begin;
        std::cerr << source.string() << ':' << position.line << ':' << position.column
                  << ": " << diagnostic.code << ": "
                  << diagnostic.message << '\n';
    }
}

template <typename Diagnostics, typename CodeName>
void print_diagnostics(const fs::path& source, const Diagnostics& diagnostics,
                       CodeName code_name) {
    for (const auto& diagnostic : diagnostics) {
        const auto& position = diagnostic.span.begin;
        std::cerr << source.string() << ':' << position.line << ':' << position.column
                  << ": " << code_name(diagnostic.code) << ": "
                  << diagnostic.message << '\n';
    }
}

std::optional<std::string> read_source(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    std::string source{std::istreambuf_iterator<char>{input},
                       std::istreambuf_iterator<char>{}};
    if (input.bad()) return std::nullopt;
    return source;
}

bool write_result_csv(const fs::path& path,
                      const domain::CharacterizationResult& result) {
    try {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) return false;
        output::write_csv(stream, result);
        stream.flush();
        if (!stream) return false;
        stream.close();
        return static_cast<bool>(stream);
    } catch (const std::ios_base::failure&) {
        return false;
    }
}

enum class JsonWriteResult { Success, IoError, SerializationError };

JsonWriteResult write_result_json(const fs::path& path,
                                  const domain::CharacterizationResult& result) {
    try {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) return JsonWriteResult::IoError;
        output::write_json(stream, result);
        stream.flush();
        if (!stream) return JsonWriteResult::IoError;
        stream.close();
        return stream ? JsonWriteResult::Success : JsonWriteResult::IoError;
    } catch (const std::ios_base::failure&) {
        return JsonWriteResult::IoError;
    } catch (const std::exception&) {
        return JsonWriteResult::SerializationError;
    }
}

void print_summary(const ir::Program& program,
                   const domain::CharacterizationResult& result,
                   const std::optional<fs::path>& csv,
                   const std::optional<fs::path>& json) {
    const auto& metrics = result.metrics;
    std::cout.imbue(std::locale::classic());
    std::cout << std::setprecision(std::numeric_limits<double>::max_digits10)
              << "characterization: " << program.characterization_name << '\n'
              << "samples: " << result.samples.size() << '\n'
              << "calibration: " << result.calibration.name << '\n'
              << "uncorrected_rms_error_db: " << metrics.uncorrected_rms_error_db << '\n'
              << "corrected_rms_error_db: " << metrics.corrected_rms_error_db << '\n'
              << "maximum_absolute_error_db: " << metrics.maximum_absolute_error_db << '\n'
              << "corrected_maximum_absolute_error_db: "
              << metrics.corrected_maximum_absolute_error_db << '\n'
              << "rms_improvement_ratio: " << metrics.rms_improvement_ratio << '\n';
    if (csv) std::cout << "csv: " << csv->string() << '\n';
    if (json) std::cout << "json: " << json->string() << '\n';
    if (!program.assertions.empty()) {
        const auto& qualification = result.qualification;
        std::cout << "qualification: "
                  << (qualification.status == domain::QualificationStatus::Pass ? "pass" : "fail")
                  << '\n'
                  << "qualification_points: " << qualification.total_points << '\n'
                  << "qualification_passed_points: " << qualification.passed_points << '\n'
                  << "qualification_failed_points: " << qualification.failed_points << '\n';
        for (const auto& assertion : qualification.assertions) {
            std::cout << assertion.id << ": total=" << assertion.total
                      << " passed=" << assertion.passed
                      << " failed=" << assertion.failed << '\n';
        }
    }
}

void print_qualification_failures(const fs::path& source,
                                  const domain::CharacterizationResult& result) {
    for (const auto& sample : result.samples) {
        for (const auto& assertion : sample.assertions) {
            if (assertion.passed) continue;
            std::cerr << source.string() << ':' << assertion.source.begin.line << ':'
                      << assertion.source.begin.column
                      << ": qualification.assertion_failed: " << assertion.id
                      << " failed\n";
        }
    }
}

int run(const Arguments& arguments) {
    if (arguments.csv && same_path(arguments.source, *arguments.csv)) {
        return report_usage("source and CSV paths must be different");
    }
    if (arguments.json && same_path(arguments.source, *arguments.json)) {
        return report_usage("source and JSON paths must be different");
    }

    auto source = read_source(arguments.source);
    if (!source) {
        std::cerr << "horusrf-run: cannot read source file '"
                  << arguments.source.string() << "'\n";
        return io_error;
    }

    if (arguments.structured_diagnostics) {
        const auto result = ai::validate_script(*source);
        ai::write_validation_json(std::cout, result);
        return result.ok() ? 0 : diagnostic_error;
    }

    auto validated = frontend::validate(*source);
    if (!validated.ok()) {
        print_frontend_diagnostics(arguments.source, validated.diagnostics);
        return diagnostic_error;
    }
    auto lowered = ir::lower(*validated.program);
    if (!lowered.ok()) {
        print_diagnostics(arguments.source, lowered.diagnostics, ir::diagnostic_code_name);
        return diagnostic_error;
    }

    if (arguments.check) return 0;

    if (arguments.csv &&
        (lowered.program->sweeps.size() != 1 ||
         lowered.program->sweeps.front().dimension != ir::SweepDimension::Frequency ||
         lowered.program->measurements.size() != 1 ||
         !lowered.program->measurements.front().legacy ||
         !lowered.program->named_values.empty() ||
         !lowered.program->assertions.empty())) {
        const auto& position = lowered.program->span.begin;
        std::cerr << arguments.source.string() << ':' << position.line << ':'
                  << position.column
                  << ": output.csv_unrepresentable: the legacy CSV schema supports only a single frequency sweep\n";
        return diagnostic_error;
    }

    device::SimulatedRfConnection rf_output;
    device::SimulatedRfTester tester{rf_output};
    device::SimulatedMeasurementDevice measurement{rf_output};
    device::SimulatedTemperatureController temperature{rf_output};
    auto execution = runtime::execute_characterization(
        *lowered.program,
        runtime::DeviceCapabilities{tester, &measurement, &temperature,
                                    &measurement, &measurement});
    if (!execution.ok()) {
        print_diagnostics(arguments.source, execution.diagnostics,
                          runtime::diagnostic_code_name);
        return diagnostic_error;
    }

    if (arguments.csv && !write_result_csv(*arguments.csv, *execution.result)) {
        std::error_code ignored;
        fs::remove(*arguments.csv, ignored);
        std::cerr << "horusrf-run: cannot write CSV file '"
                  << arguments.csv->string() << "'\n";
        return io_error;
    }
    const auto json_write = arguments.json
                                ? write_result_json(*arguments.json, *execution.result)
                                : JsonWriteResult::Success;
    if (json_write != JsonWriteResult::Success) {
        std::error_code ignored;
        fs::remove(*arguments.json, ignored);
        if (json_write == JsonWriteResult::SerializationError) {
            const auto& position = lowered.program->span.begin;
            std::cerr << arguments.source.string() << ':' << position.line << ':'
                      << position.column
                      << ": output.json_serialization_failed: cannot serialize result\n";
            return diagnostic_error;
        }
        std::cerr << "horusrf-run: cannot write JSON file '"
                  << arguments.json->string() << "'\n";
        return io_error;
    }
    print_summary(*lowered.program, *execution.result, arguments.csv, arguments.json);
    if (execution.result->qualification.status == domain::QualificationStatus::Fail) {
        print_qualification_failures(arguments.source, *execution.result);
        return qualification_failure;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        int exit_code = 0;
        auto arguments = parse_arguments(argc, argv, exit_code);
        if (!arguments) return exit_code;
        return run(*arguments);
    } catch (const std::exception& error) {
        std::cerr << "horusrf-run: " << error.what() << '\n';
        return unexpected_error;
    } catch (...) {
        std::cerr << "horusrf-run: unexpected non-standard exception\n";
        return unexpected_error;
    }
}
