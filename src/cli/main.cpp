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

#include "horusrf/device/simulated_rf_device.hpp"
#include "horusrf/ir/lowering.hpp"
#include "horusrf/output/csv.hpp"
#include "horusrf/parser/parser.hpp"
#include "horusrf/runtime/characterization.hpp"
#include "horusrf/semantic/analyzer.hpp"

namespace {
namespace fs = std::filesystem;
using namespace horusrf;

constexpr int usage_error = 2;
constexpr int io_error = 3;
constexpr int diagnostic_error = 4;
constexpr int unexpected_error = 5;
constexpr std::string_view usage =
    "usage: horusrf-run <source.hrf> [--csv <output.csv>]";

struct Arguments {
    fs::path source;
    std::optional<fs::path> csv;
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

    Arguments arguments{fs::path{argv[1]}, std::nullopt};
    if (argc == 2) return arguments;
    if (argc != 4 || std::string_view{argv[2]} != "--csv" ||
        std::string_view{argv[3]}.empty()) {
        exit_code = report_usage("expected one optional --csv path");
        return std::nullopt;
    }
    arguments.csv = fs::path{argv[3]};
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

void print_summary(const ir::Program& program,
                   const domain::CharacterizationResult& result,
                   const std::optional<fs::path>& csv) {
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
}

int run(const Arguments& arguments) {
    if (arguments.csv && same_path(arguments.source, *arguments.csv)) {
        return report_usage("source and CSV paths must be different");
    }

    auto source = read_source(arguments.source);
    if (!source) {
        std::cerr << "horusrf-run: cannot read source file '"
                  << arguments.source.string() << "'\n";
        return io_error;
    }

    ast::Program syntax;
    try {
        syntax = parser::parse(*source);
    } catch (const parser::ParseError& error) {
        const std::vector<parser::Diagnostic> diagnostics{error.diagnostic()};
        print_diagnostics(arguments.source, diagnostics, parser::diagnostic_code_name);
        return diagnostic_error;
    }

    auto analyzed = semantic::analyze(syntax);
    if (!analyzed.ok()) {
        print_diagnostics(arguments.source, analyzed.diagnostics,
                          semantic::diagnostic_code_name);
        return diagnostic_error;
    }
    auto lowered = ir::lower(*analyzed.program);
    if (!lowered.ok()) {
        print_diagnostics(arguments.source, lowered.diagnostics, ir::diagnostic_code_name);
        return diagnostic_error;
    }

    device::SimulatedRfDevice simulator;
    auto execution = runtime::execute_characterization(*lowered.program, simulator);
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
    print_summary(*lowered.program, *execution.result, arguments.csv);
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
