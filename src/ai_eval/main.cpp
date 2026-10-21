#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "horusrf/ai/evaluation.hpp"
#include "horusrf/ai/ollama_provider.hpp"

namespace {

struct Options final {
    std::filesystem::path dataset{"tests/ai/golden"};
    std::filesystem::path output{"build/ai-evaluation/results.json"};
    std::vector<std::string> models;
    std::set<std::string, std::less<>> cases;
    std::string endpoint{"http://127.0.0.1:11434"};
    std::optional<std::string> git_revision;
    double temperature{0.0};
    std::size_t max_output_tokens{2048};
    std::size_t maximum_repairs{3};
    std::size_t repetitions{1};
    std::chrono::milliseconds timeout{std::chrono::minutes{2}};
};

std::vector<std::string> split(std::string_view input) {
    std::vector<std::string> values;
    std::size_t begin{};
    while (begin <= input.size()) {
        const auto end = input.find(',', begin);
        const auto part = input.substr(begin, end == std::string_view::npos
                                                 ? input.size() - begin
                                                 : end - begin);
        if (part.empty()) throw std::invalid_argument("comma-separated values must not be empty");
        values.emplace_back(part);
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return values;
}

std::string value_after(int& index, int argc, char** argv, std::string_view option) {
    if (++index >= argc) throw std::invalid_argument(std::string{option} + " requires a value");
    return argv[index];
}

std::size_t positive_size(std::string_view value, std::string_view option) {
    std::size_t consumed{};
    const auto parsed = std::stoull(std::string{value}, &consumed);
    if (consumed != value.size() || parsed == 0) {
        throw std::invalid_argument(std::string{option} + " must be a positive integer");
    }
    return static_cast<std::size_t>(parsed);
}

void usage(std::ostream& output) {
    output << "Usage: horusrf-ai-eval --models MODEL[,MODEL...] [options]\n"
              "  --dataset PATH             Golden dataset root\n"
              "  --output PATH              Versioned JSON report path\n"
              "  --case ID[,ID...]          Evaluate only selected cases\n"
              "  --repeat COUNT             Repeat each selected case\n"
              "  --temperature VALUE        Inference temperature\n"
              "  --max-output-tokens COUNT  Provider output limit\n"
              "  --timeout-seconds COUNT    Per-provider-call timeout\n"
              "  --max-repairs COUNT        Bounded compiler repair attempts\n"
              "  --endpoint URL             Local Ollama HTTP endpoint\n"
              "  --git-revision REVISION    Source revision recorded in the report\n";
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            usage(std::cout);
            std::exit(0);
        } else if (argument == "--models") {
            options.models = split(value_after(index, argc, argv, argument));
        } else if (argument == "--dataset") {
            options.dataset = value_after(index, argc, argv, argument);
        } else if (argument == "--output") {
            options.output = value_after(index, argc, argv, argument);
        } else if (argument == "--case") {
            for (auto& value : split(value_after(index, argc, argv, argument))) {
                options.cases.insert(std::move(value));
            }
        } else if (argument == "--repeat") {
            options.repetitions = positive_size(value_after(index, argc, argv, argument), argument);
        } else if (argument == "--temperature") {
            const std::string value = value_after(index, argc, argv, argument);
            std::size_t consumed{};
            options.temperature = std::stod(value, &consumed);
            if (consumed != value.size() || options.temperature < 0.0) {
                throw std::invalid_argument("--temperature must be a non-negative number");
            }
        } else if (argument == "--max-output-tokens") {
            options.max_output_tokens = positive_size(value_after(index, argc, argv, argument), argument);
        } else if (argument == "--timeout-seconds") {
            options.timeout = std::chrono::seconds{
                positive_size(value_after(index, argc, argv, argument), argument)};
        } else if (argument == "--max-repairs") {
            options.maximum_repairs = positive_size(value_after(index, argc, argv, argument), argument);
        } else if (argument == "--endpoint") {
            options.endpoint = value_after(index, argc, argv, argument);
        } else if (argument == "--git-revision") {
            options.git_revision = value_after(index, argc, argv, argument);
        } else {
            throw std::invalid_argument("unknown option: " + std::string{argument});
        }
    }
    if (options.models.empty()) throw std::invalid_argument("--models is required");
    return options;
}

std::string utc_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &value);
#else
    gmtime_r(&value, &utc);
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        auto loaded = horusrf::ai::load_golden_dataset(options.dataset);
        if (!loaded.ok()) {
            for (const auto& diagnostic : loaded.diagnostics) {
                std::cerr << diagnostic.path.string() << ": " << diagnostic.message << '\n';
            }
            return 2;
        }

        horusrf::ai::GoldenDataset dataset = std::move(*loaded.dataset);
        if (!options.cases.empty()) {
            std::erase_if(dataset.cases, [&](const auto& item) {
                return !options.cases.contains(item.metadata.id);
            });
            for (const auto& requested : options.cases) {
                const bool found = std::ranges::any_of(dataset.cases, [&](const auto& item) {
                    return item.metadata.id == requested;
                });
                if (!found) throw std::invalid_argument("unknown golden case: " + requested);
            }
        }
        if (dataset.cases.empty()) throw std::invalid_argument("no golden cases were selected");

        horusrf::ai::OllamaProvider provider{{options.endpoint}};
        const auto available_result = provider.list_models(std::chrono::seconds{5});
        if (const auto* error = std::get_if<horusrf::ai::ProviderError>(&available_result)) {
            std::cerr << "Unable to query Ollama models: " << error->message << '\n';
            return 3;
        }
        const auto& available = std::get<std::vector<std::string>>(available_result);
        for (const auto& model : options.models) {
            if (std::ranges::find(available, model) == available.end()) {
                std::cerr << "Requested model is not installed: " << model << '\n';
                return 3;
            }
        }

        const horusrf::ai::EvaluationSettings settings{
            options.models, options.temperature, options.max_output_tokens,
            options.timeout, options.maximum_repairs, options.repetitions,
            utc_timestamp(), options.git_revision};
        const auto report = horusrf::ai::evaluate_golden_dataset(
            dataset, provider, settings);

        if (!options.output.parent_path().empty()) {
            std::filesystem::create_directories(options.output.parent_path());
        }
        std::ofstream report_file{options.output, std::ios::binary};
        if (!report_file) throw std::runtime_error("cannot open report output: " + options.output.string());
        horusrf::ai::write_evaluation_json(report_file, report);
        if (!report_file) throw std::runtime_error("failed to write report: " + options.output.string());
        horusrf::ai::write_evaluation_table(std::cout, report);
        std::cout << "JSON report: " << options.output.string() << '\n';
        return 0;
    } catch (const std::invalid_argument& error) {
        std::cerr << "Argument error: " << error.what() << '\n';
        usage(std::cerr);
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Evaluation failed: " << error.what() << '\n';
        return 3;
    }
}
