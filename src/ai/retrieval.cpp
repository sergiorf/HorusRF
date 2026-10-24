#include "horusrf/ai/retrieval.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <variant>

#include "horusrf/ai/golden_dataset.hpp"
#include "horusrf/ai/requirement_verification.hpp"
#include "horusrf/ai/support_assessment.hpp"
#include "horusrf/ai/validation.hpp"

namespace horusrf::ai {
namespace {

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue, std::less<>>;

struct JsonValue final {
    using Storage = std::variant<std::nullptr_t, bool, double, std::string,
                                 JsonArray, JsonObject>;
    Storage value;
};

class JsonParser final {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    JsonValue parse() {
        auto value = parse_value();
        space();
        if (position_ != input_.size()) fail("unexpected trailing content");
        return value;
    }

private:
    [[noreturn]] void fail(std::string_view message) const {
        throw std::runtime_error(std::string{message} + " at byte " +
                                 std::to_string(position_));
    }
    void space() {
        while (position_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[position_])) != 0) {
            ++position_;
        }
    }
    bool consume(char expected) {
        space();
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }
    void require(char expected) {
        if (!consume(expected)) fail(std::string{"expected '"} + expected + "'");
    }
    JsonValue parse_value() {
        space();
        if (position_ >= input_.size()) fail("expected a JSON value");
        switch (input_[position_]) {
        case '{': return JsonValue{parse_object()};
        case '[': return JsonValue{parse_array()};
        case '"': return JsonValue{parse_string()};
        case 't': return literal("true", JsonValue{true});
        case 'f': return literal("false", JsonValue{false});
        case 'n': return literal("null", JsonValue{nullptr});
        default:
            if (input_[position_] == '-' ||
                std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                return JsonValue{parse_number()};
            }
            break;
        }
        fail("invalid JSON value");
    }
    JsonValue literal(std::string_view text, JsonValue value) {
        if (input_.substr(position_, text.size()) != text) fail("invalid JSON literal");
        position_ += text.size();
        return value;
    }
    JsonObject parse_object() {
        require('{');
        JsonObject object;
        if (consume('}')) return object;
        while (true) {
            space();
            if (position_ >= input_.size() || input_[position_] != '"') {
                fail("expected an object member name");
            }
            auto name = parse_string();
            require(':');
            auto [unused, inserted] = object.emplace(std::move(name), parse_value());
            static_cast<void>(unused);
            if (!inserted) fail("duplicate object member");
            if (consume('}')) return object;
            require(',');
        }
    }
    JsonArray parse_array() {
        require('[');
        JsonArray array;
        if (consume(']')) return array;
        while (true) {
            array.push_back(parse_value());
            if (consume(']')) return array;
            require(',');
        }
    }
    static unsigned hex(char value) {
        if (value >= '0' && value <= '9') return static_cast<unsigned>(value - '0');
        if (value >= 'a' && value <= 'f') return 10U + static_cast<unsigned>(value - 'a');
        if (value >= 'A' && value <= 'F') return 10U + static_cast<unsigned>(value - 'A');
        throw std::runtime_error("invalid unicode escape");
    }
    void append_utf8(std::string& output, unsigned codepoint) const {
        if (codepoint <= 0x7fU) output.push_back(static_cast<char>(codepoint));
        else if (codepoint <= 0x7ffU) {
            output.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        } else {
            output.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        }
    }
    std::string parse_string() {
        require('"');
        std::string output;
        while (position_ < input_.size()) {
            const char current = input_[position_++];
            if (current == '"') return output;
            if (static_cast<unsigned char>(current) < 0x20U) fail("control character in string");
            if (current != '\\') { output.push_back(current); continue; }
            if (position_ >= input_.size()) fail("incomplete string escape");
            switch (const char escaped = input_[position_++]) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u': {
                if (position_ + 4U > input_.size()) fail("incomplete unicode escape");
                unsigned codepoint{};
                try {
                    for (int index = 0; index < 4; ++index) {
                        codepoint = (codepoint << 4U) | hex(input_[position_++]);
                    }
                } catch (const std::runtime_error&) { fail("invalid unicode escape"); }
                if (codepoint >= 0xd800U && codepoint <= 0xdfffU) {
                    fail("unicode surrogate escapes are not supported");
                }
                append_utf8(output, codepoint);
                break;
            }
            default: fail("invalid string escape");
            }
        }
        fail("unterminated string");
    }
    double parse_number() {
        space();
        const auto begin = position_;
        if (input_[position_] == '-') ++position_;
        while (position_ < input_.size() &&
               std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) ++position_;
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) ++position_;
        }
        const auto token = input_.substr(begin, position_ - begin);
        try {
            std::size_t consumed{};
            const auto result = std::stod(std::string{token}, &consumed);
            if (consumed != token.size() || !std::isfinite(result)) fail("invalid number");
            return result;
        } catch (const std::exception&) { fail("invalid number"); }
    }

    std::string_view input_;
    std::size_t position_{};
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open file");
    std::ostringstream output;
    output << input.rdbuf();
    if (!input.eof() && input.fail()) throw std::runtime_error("cannot read file");
    return output.str();
}

bool valid_utf8(std::string_view text) {
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7fU) { ++index; continue; }
        std::size_t count{};
        unsigned codepoint{};
        if ((first & 0xe0U) == 0xc0U) { count = 2; codepoint = first & 0x1fU; }
        else if ((first & 0xf0U) == 0xe0U) { count = 3; codepoint = first & 0x0fU; }
        else if ((first & 0xf8U) == 0xf0U) { count = 4; codepoint = first & 0x07U; }
        else return false;
        if (index + count > text.size()) return false;
        for (std::size_t offset = 1; offset < count; ++offset) {
            const auto byte = static_cast<unsigned char>(text[index + offset]);
            if ((byte & 0xc0U) != 0x80U) return false;
            codepoint = (codepoint << 6U) | (byte & 0x3fU);
        }
        if ((count == 2 && codepoint < 0x80U) ||
            (count == 3 && codepoint < 0x800U) ||
            (count == 4 && codepoint < 0x10000U) || codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU)) return false;
        index += count;
    }
    return true;
}

std::string read_utf8_file(const std::filesystem::path& path) {
    auto result = read_file(path);
    if (!valid_utf8(result)) throw std::runtime_error("file is not valid UTF-8");
    return result;
}

bool valid_identifier(std::string_view value) {
    if (value.empty() || std::islower(static_cast<unsigned char>(value.front())) == 0) {
        return false;
    }
    for (const unsigned char byte : value.substr(1)) {
        if (std::islower(byte) == 0 && std::isdigit(byte) == 0 && byte != '_') return false;
    }
    return true;
}

const JsonObject& object(const JsonValue& value, std::string_view context) {
    const auto* result = std::get_if<JsonObject>(&value.value);
    if (result == nullptr) throw std::runtime_error(std::string{context} + " must be an object");
    return *result;
}

const JsonValue& member(const JsonObject& value, std::string_view name) {
    const auto found = value.find(name);
    if (found == value.end()) throw std::runtime_error("missing required member '" + std::string{name} + "'");
    return found->second;
}

std::string string_member(const JsonObject& value, std::string_view name) {
    const auto* result = std::get_if<std::string>(&member(value, name).value);
    if (result == nullptr || result->empty()) {
        throw std::runtime_error("member '" + std::string{name} + "' must be a non-empty string");
    }
    return *result;
}

double number_member(const JsonObject& value, std::string_view name) {
    const auto* result = std::get_if<double>(&member(value, name).value);
    if (result == nullptr) throw std::runtime_error("member '" + std::string{name} + "' must be a number");
    return *result;
}

void exact_members(const JsonObject& value, std::initializer_list<std::string_view> names) {
    std::set<std::string, std::less<>> expected;
    for (const auto name : names) expected.emplace(name);
    for (const auto& [name, unused] : value) {
        static_cast<void>(unused);
        if (!expected.contains(name)) throw std::runtime_error("unknown member '" + name + "'");
    }
}

std::vector<std::string> strings(const JsonObject& value, std::string_view name,
                                 bool allow_empty = false) {
    const auto* array = std::get_if<JsonArray>(&member(value, name).value);
    if (array == nullptr || (!allow_empty && array->empty())) {
        throw std::runtime_error("member '" + std::string{name} + "' must be a " +
                                 (allow_empty ? "string array" : "non-empty string array"));
    }
    std::vector<std::string> output;
    std::set<std::string, std::less<>> unique;
    for (const auto& item : *array) {
        const auto* text = std::get_if<std::string>(&item.value);
        if (text == nullptr || text->empty()) {
            throw std::runtime_error("member '" + std::string{name} + "' contains an invalid string");
        }
        if (!unique.insert(*text).second) {
            throw std::runtime_error("member '" + std::string{name} + "' contains duplicate value '" + *text + "'");
        }
        output.push_back(*text);
    }
    return output;
}

struct Manifest final {
    std::string corpus_id;
    std::string version;
    std::string digest;
    std::vector<std::string> examples;
};

Manifest parse_manifest(std::string_view source) {
    const auto parsed = JsonParser{source}.parse();
    const auto& value = object(parsed, "manifest");
    exact_members(value, {"schema", "version", "corpus_id", "corpus_version",
                          "content_digest", "examples"});
    if (string_member(value, "schema") != "horusrf.retrieval-corpus" ||
        number_member(value, "version") != 1.0) {
        throw std::runtime_error("unsupported retrieval corpus schema version");
    }
    auto result = Manifest{string_member(value, "corpus_id"),
                           string_member(value, "corpus_version"),
                           string_member(value, "content_digest"),
                           strings(value, "examples")};
    for (const auto& id : result.examples) {
        if (!valid_identifier(id)) {
            throw std::runtime_error("invalid example directory id '" + id + "'");
        }
    }
    return result;
}

RetrievalExampleMetadata parse_metadata(std::string_view source) {
    const auto parsed = JsonParser{source}.parse();
    const auto& value = object(parsed, "metadata");
    exact_members(value, {"schema", "version", "id", "category", "operations",
                          "structural_steps", "sweep_dimensions", "dependencies",
                          "comparison_strictness", "complexity", "provenance"});
    if (string_member(value, "schema") != "horusrf.retrieval-example" ||
        number_member(value, "version") != 1.0) {
        throw std::runtime_error("unsupported retrieval example schema version");
    }
    const auto dimensions = strings(value, "sweep_dimensions", true);
    for (const auto& dimension : dimensions) {
        if (dimension != "frequency" && dimension != "temperature") {
            throw std::runtime_error("unsupported sweep dimension '" + dimension + "'");
        }
    }
    const auto* dependency_array = std::get_if<JsonArray>(&member(value, "dependencies").value);
    if (dependency_array == nullptr) throw std::runtime_error("member 'dependencies' must be an array");
    std::vector<RetrievalDependency> dependencies;
    for (const auto& item : *dependency_array) {
        const auto& dependency = object(item, "dependency");
        exact_members(dependency, {"producer", "consumer"});
        dependencies.push_back({string_member(dependency, "producer"),
                                string_member(dependency, "consumer")});
    }
    const auto& provenance = object(member(value, "provenance"), "provenance");
    exact_members(provenance, {"origin", "authoring_date", "influenced_by"});
    auto strictness = string_member(value, "comparison_strictness");
    if (strictness != "none" && strictness != "strict" && strictness != "inclusive" &&
        strictness != "mixed") throw std::runtime_error("unsupported comparison_strictness");
    auto complexity = string_member(value, "complexity");
    if (complexity != "basic" && complexity != "intermediate" && complexity != "advanced") {
        throw std::runtime_error("unsupported complexity '" + complexity + "'");
    }
    auto id = string_member(value, "id");
    if (!valid_identifier(id)) throw std::runtime_error("invalid example id '" + id + "'");
    auto category = string_member(value, "category");
    if (!valid_identifier(category)) throw std::runtime_error("invalid category '" + category + "'");
    auto operations = strings(value, "operations");
    static const std::set<std::string, std::less<>> supported_operations{
        "reference_power", "sweep_frequency", "sweep_temperature", "measure_power",
        "measure_evm", "measure_frequency_error", "derive_power_error",
        "derive_power_delta", "assert_power", "assert_evm", "assert_frequency_error",
        "assert_power_delta", "derive_calibration"};
    for (const auto& operation : operations) {
        if (!supported_operations.contains(operation)) {
            throw std::runtime_error("unsupported operation '" + operation + "'");
        }
    }
    auto steps = strings(value, "structural_steps");
    for (const auto& step : steps) {
        if (!valid_identifier(step)) throw std::runtime_error("invalid structural step '" + step + "'");
    }
    return {std::move(id), std::move(category), std::move(operations), std::move(steps),
            dimensions, std::move(dependencies), std::move(strictness),
            std::move(complexity),
            {string_member(provenance, "origin"), string_member(provenance, "authoring_date"),
             strings(provenance, "influenced_by", true)}};
}

std::uint64_t hash_bytes(std::uint64_t hash, std::string_view text) {
    constexpr std::uint64_t prime = 1099511628211ULL;
    for (const unsigned char byte : text) { hash ^= byte; hash *= prime; }
    return hash;
}

std::string digest_files(const std::filesystem::path& root, const Manifest& manifest) {
    std::uint64_t hash = 14695981039346656037ULL;
    hash = hash_bytes(hash, manifest.corpus_id);
    hash = hash_bytes(hash, manifest.version);
    for (const auto& id : manifest.examples) {
        hash = hash_bytes(hash, id);
        for (const auto filename : {"request.txt", "program.hrf", "metadata.json"}) {
            hash = hash_bytes(hash, filename);
            hash = hash_bytes(hash, read_file(root / "examples" / id / filename));
        }
    }
    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}

std::string normalized_text(std::string_view text) {
    std::string output;
    bool separated = true;
    for (const unsigned char byte : text) {
        if (std::isalnum(byte) != 0 || byte == '_') {
            output.push_back(static_cast<char>(std::tolower(byte)));
            separated = false;
        } else if (!separated) {
            output.push_back(' ');
            separated = true;
        }
    }
    if (!output.empty() && output.back() == ' ') output.pop_back();
    return output;
}

std::set<std::string, std::less<>> words(std::string_view text) {
    std::set<std::string, std::less<>> result;
    std::istringstream input(normalized_text(text));
    for (std::string word; input >> word;) result.insert(std::move(word));
    return result;
}

double jaccard(std::string_view left, std::string_view right) {
    const auto a = words(left);
    const auto b = words(right);
    std::size_t intersection{};
    for (const auto& item : a) intersection += b.contains(item) ? 1U : 0U;
    const auto count = a.size() + b.size() - intersection;
    return count == 0 ? 1.0 : static_cast<double>(intersection) / static_cast<double>(count);
}

std::string normalized_program(std::string_view source, bool erase_values) {
    std::string without_comments;
    for (std::size_t index = 0; index < source.size();) {
        if (index + 1 < source.size() && source[index] == '/' && source[index + 1] == '/') {
            while (index < source.size() && source[index] != '\n') ++index;
        } else { without_comments.push_back(source[index++]); }
    }
    if (!erase_values) {
        std::string compact;
        for (const unsigned char byte : without_comments) {
            if (std::isspace(byte) == 0) compact.push_back(static_cast<char>(byte));
        }
        return compact;
    }
    static const std::set<std::string, std::less<>> reserved{
        "characterize", "reference", "power", "sweep", "frequency", "temperature",
        "step", "measure", "derive", "calibration", "correction", "over", "assert",
        "abs", "evm", "frequency_error", "hz", "khz", "mhz", "ghz", "dbm", "db",
        "k", "degc", "deltak", "deltac"};
    std::string output;
    const auto append = [&output](std::string_view token) {
        if (!output.empty()) output.push_back(' ');
        output.append(token);
    };
    for (std::size_t index = 0; index < without_comments.size();) {
        const auto byte = static_cast<unsigned char>(without_comments[index]);
        if (std::isspace(byte) != 0) { ++index; continue; }
        if (std::isdigit(byte) != 0) {
            while (index < without_comments.size() &&
                   (std::isdigit(static_cast<unsigned char>(without_comments[index])) != 0 ||
                    without_comments[index] == '.')) ++index;
            append("#");
            continue;
        }
        if (std::isalpha(byte) != 0 || byte == '_') {
            const auto begin = index++;
            while (index < without_comments.size()) {
                const auto current = static_cast<unsigned char>(without_comments[index]);
                if (std::isalnum(current) == 0 && current != '_') break;
                ++index;
            }
            auto token = without_comments.substr(begin, index - begin);
            std::ranges::transform(token, token.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            append(reserved.contains(token) ? std::string_view{token} : std::string_view{"$"});
            continue;
        }
        if (index + 1 < without_comments.size()) {
            const auto pair = std::string_view{without_comments}.substr(index, 2);
            if (pair == "<=" || pair == ">=" || pair == "==" || pair == "!=" ||
                pair == "..") {
                append(pair);
                index += 2;
                continue;
            }
        }
        append(std::string_view{without_comments}.substr(index, 1));
        ++index;
    }
    return output;
}

std::string program_shape(std::string_view source) {
    auto result = normalized_program(source, true);
    const auto facts = compile_program_facts(source);
    if (!facts) return result;
    result += " |sweeps";
    for (const auto& sweep : facts->sweeps) {
        result += sweep.dimension == ir::SweepDimension::Frequency ? ":frequency" : ":temperature";
        result += sweep.point_count == 1 ? ":single" : ":range";
    }
    result += " |measurements";
    for (const auto& measurement : facts->measurements) {
        switch (measurement.operation) {
        case ir::MeasurementOperation::Power: result += ":power"; break;
        case ir::MeasurementOperation::Evm: result += ":evm"; break;
        case ir::MeasurementOperation::FrequencyError: result += ":frequency_error"; break;
        }
    }
    result += " |derived:" + std::to_string(facts->derived_values.size());
    result += " |assertions:" + std::to_string(facts->assertions.size());
    return result;
}

void add_leakage(LeakageReport& report, const RetrievalExample& example,
                 std::string_view comparison_id, std::string_view request,
                 std::string_view program) {
    const auto request_similarity = jaccard(example.request, request);
    if (normalized_text(example.request) == normalized_text(request)) {
        report.findings.push_back({example.metadata.id, std::string{comparison_id},
                                   LeakageMatchKind::ExactRequest, 1.0});
    } else if (request_similarity >= 0.72) {
        report.findings.push_back({example.metadata.id, std::string{comparison_id},
                                   LeakageMatchKind::LikelyRequestParaphrase,
                                   request_similarity});
    }
    if (normalized_program(example.program, false) == normalized_program(program, false)) {
        report.findings.push_back({example.metadata.id, std::string{comparison_id},
                                   LeakageMatchKind::ExactProgram, 1.0});
    } else if (program_shape(example.program) == program_shape(program)) {
        report.findings.push_back({example.metadata.id, std::string{comparison_id},
                                   LeakageMatchKind::LikelyProgramClone, 1.0});
    }
}

void diagnostic(RetrievalCorpusResult& result, const std::filesystem::path& path,
                std::string message) {
    result.diagnostics.push_back({path, std::move(message)});
}

std::string validation_summary(const ValidationResult& validation) {
    if (validation.diagnostics().empty()) return "no compiler diagnostic";
    return validation.diagnostics().front().code + ": " +
           validation.diagnostics().front().message;
}

std::string json_string(std::string_view text) {
    std::ostringstream output;
    output << '"';
    for (const unsigned char byte : text) {
        if (byte == '"') output << "\\\"";
        else if (byte == '\\') output << "\\\\";
        else if (byte == '\n') output << "\\n";
        else if (byte < 0x20U) output << "\\u00" << std::hex << std::setw(2)
                                      << std::setfill('0') << static_cast<unsigned>(byte);
        else output << static_cast<char>(byte);
    }
    output << '"';
    return output.str();
}

std::string_view kind_name(LeakageMatchKind kind) {
    switch (kind) {
    case LeakageMatchKind::ExactRequest: return "exact_request";
    case LeakageMatchKind::LikelyRequestParaphrase: return "likely_request_paraphrase";
    case LeakageMatchKind::ExactProgram: return "exact_program";
    case LeakageMatchKind::LikelyProgramClone: return "likely_program_clone";
    }
    return "unknown";
}

template <typename Range>
bool contains(const Range& values, std::string_view value) {
    return std::ranges::find(values, value) != values.end();
}

void add_unique(std::vector<std::string>& values, std::string value) {
    if (!contains(values, value)) values.push_back(std::move(value));
}

std::string dimension_name(ir::SweepDimension dimension) {
    return dimension == ir::SweepDimension::Frequency ? "frequency" : "temperature";
}

std::string measurement_operation(ir::MeasurementOperation operation) {
    switch (operation) {
    case ir::MeasurementOperation::Power: return "measure_power";
    case ir::MeasurementOperation::Evm: return "measure_evm";
    case ir::MeasurementOperation::FrequencyError: return "measure_frequency_error";
    }
    return {};
}

std::string assertion_operation(ir::ValueType type) {
    switch (type) {
    case ir::ValueType::Power: return "assert_power";
    case ir::ValueType::PowerDelta: return "assert_power_delta";
    case ir::ValueType::Ratio: return "assert_evm";
    case ir::ValueType::Frequency:
    case ir::ValueType::FrequencyDelta: return "assert_frequency_error";
    default: return {};
    }
}

std::string merge_strictness(std::string current, std::string_view next) {
    if (next == "none") return current;
    if (current == "none" || current == next) return std::string{next};
    return "mixed";
}

std::string assertion_strictness(ir::ComparisonOperator operation) {
    switch (operation) {
    case ir::ComparisonOperator::Less:
    case ir::ComparisonOperator::Greater: return "strict";
    case ir::ComparisonOperator::LessEqual:
    case ir::ComparisonOperator::GreaterEqual:
    case ir::ComparisonOperator::Equal:
    case ir::ComparisonOperator::NotEqual: return "inclusive";
    }
    return "none";
}

double ordered_dimension_score(const std::vector<std::string>& requested,
                               const std::vector<std::string>& candidate) {
    if (requested.empty()) return 0.0;
    if (requested == candidate) return 18.0;
    std::size_t ordered{};
    std::size_t cursor{};
    for (const auto& dimension : candidate) {
        if (cursor < requested.size() && dimension == requested[cursor]) {
            ++cursor;
            ++ordered;
        }
    }
    return 10.0 * static_cast<double>(ordered) /
           static_cast<double>(requested.size());
}

double coverage(const std::vector<std::string>& requested,
                const std::vector<std::string>& candidate) {
    if (requested.empty()) return 1.0;
    const auto count = std::ranges::count_if(requested, [&](const auto& operation) {
        return contains(candidate, operation);
    });
    return static_cast<double>(count) / static_cast<double>(requested.size());
}

bool is_foundational_operation(std::string_view operation) {
    return operation == "reference_power" || operation == "derive_calibration";
}

std::size_t context_bytes(const RetrievalExample& example) {
    return example.request.size() + example.program.size();
}

std::size_t estimated_tokens(std::size_t bytes) {
    return (bytes + 3U) / 4U;
}

bool near_duplicate(const RetrievalExample& left, const RetrievalExample& right) {
    return jaccard(left.request, right.request) >= 0.72 ||
           program_shape(left.program) == program_shape(right.program);
}

struct RankedExample final {
    std::size_t manifest_index{};
    RetrievalComponentScores scores;
    bool complete{};
    std::string rationale;
};

RankedExample rank_example(const RetrievalQuery& query,
                           const RetrievalExample& example,
                           std::size_t manifest_index) {
    RetrievalComponentScores scores;
    const auto operation_coverage = coverage(query.operations,
                                             example.metadata.operations);
    scores.operation_coverage = 42.0 * operation_coverage;
    scores.structural = ordered_dimension_score(query.sweep_dimensions,
                                                example.metadata.sweep_dimensions);
    if (query.fixed_frequency &&
        contains(example.metadata.structural_steps, "sweep_single_frequency_point")) {
        scores.structural += 6.0;
    }
    if (query.named_values && std::ranges::any_of(
            example.metadata.structural_steps, [](const auto& step) {
                return step.find("named") != std::string::npos;
            })) {
        scores.structural += 3.0;
    }
    if (query.dependency_chain && example.metadata.dependencies.size() >= 2U) {
        scores.structural += 5.0;
    }

    std::size_t typed_requested{};
    std::size_t typed_matched{};
    if (query.comparison_strictness != "none") {
        ++typed_requested;
        if (example.metadata.comparison_strictness == query.comparison_strictness ||
            example.metadata.comparison_strictness == "mixed") ++typed_matched;
    }
    if (query.calibration) {
        ++typed_requested;
        if (contains(example.metadata.operations, "derive_calibration")) ++typed_matched;
    }
    if (query.fixed_frequency) {
        ++typed_requested;
        if (contains(example.metadata.structural_steps,
                     "sweep_single_frequency_point")) ++typed_matched;
    }
    scores.typed_feature_coverage = typed_requested == 0U ? 0.0
        : 14.0 * static_cast<double>(typed_matched) /
              static_cast<double>(typed_requested);
    scores.lexical_similarity = 8.0 * jaccard(query.instruction, example.request);

    const auto complexity = query.complexity == RetrievalComplexity::Basic ? "basic"
        : query.complexity == RetrievalComplexity::Intermediate ? "intermediate"
                                                                : "advanced";
    scores.complexity_compatibility = example.metadata.complexity == complexity ? 5.0 : 0.0;

    std::size_t conflicts{};
    for (const auto& operation : example.metadata.operations) {
        if (!is_foundational_operation(operation) &&
            !contains(query.operations, operation)) ++conflicts;
    }
    for (auto unsupported : query.unsupported_operations) {
        std::ranges::replace(unsupported, '.', '_');
        if (contains(example.metadata.operations, unsupported)) conflicts += 2U;
    }
    scores.conflict_penalty = 2.5 * static_cast<double>(conflicts);
    const bool dimensions_complete = query.sweep_dimensions.empty() ||
                                     query.sweep_dimensions == example.metadata.sweep_dimensions;
    const bool complete = operation_coverage == 1.0 && dimensions_complete &&
                          typed_matched == typed_requested;
    if (complete) scores.completeness_bonus = 9.0;
    scores.total = scores.structural + scores.operation_coverage +
                   scores.typed_feature_coverage + scores.lexical_similarity +
                   scores.complexity_compatibility + scores.completeness_bonus -
                   scores.conflict_penalty;

    std::ostringstream rationale;
    rationale.imbue(std::locale::classic());
    rationale << (complete ? "complete" : "partial")
              << " structural match; operation coverage " << std::fixed
              << std::setprecision(2) << operation_coverage
              << "; ordered sweep compatibility "
              << ordered_dimension_score(query.sweep_dimensions,
                                         example.metadata.sweep_dimensions)
              << "; conflicts " << conflicts;
    return {manifest_index, scores, complete, rationale.str()};
}

} // namespace

std::string_view retrieval_ranking_id() noexcept {
    return "structural-rag-ranking-v1";
}

RetrievalQuery analyze_retrieval_query(std::string_view instruction) {
    RetrievalQuery query;
    query.instruction = std::string{instruction};
    const auto support = assess_instruction_support(instruction);
    for (const auto& clause : support.clauses) {
        if (clause.status == SupportClauseStatus::Unsupported) {
            add_unique(query.unsupported_operations, clause.operation);
            continue;
        }
        if (clause.status == SupportClauseStatus::Uncertain) {
            ++query.unknown_evidence_count;
        }
    }

    const auto requirements = extract_requirements(instruction);
    for (const auto& requirement : requirements) {
        if (requirement.confidence != RequirementConfidence::High) {
            ++query.unknown_evidence_count;
            continue;
        }
        switch (requirement.kind) {
        case RequirementKind::ReferencePower:
            if (requirement.reference_power) add_unique(query.operations, "reference_power");
            break;
        case RequirementKind::Sweep:
            if (requirement.sweep) {
                const auto dimension = dimension_name(requirement.sweep->dimension);
                add_unique(query.operations, "sweep_" + dimension);
                add_unique(query.sweep_dimensions, dimension);
                query.fixed_frequency = query.fixed_frequency ||
                    (dimension == "frequency" && requirement.sweep->point_count == 1U);
            } else {
                ++query.unknown_evidence_count;
            }
            break;
        case RequirementKind::Measurement:
            if (requirement.measurement) {
                add_unique(query.operations,
                           measurement_operation(requirement.measurement->operation));
                query.named_values = query.named_values ||
                                     requirement.constrain_measurement_name;
            } else {
                ++query.unknown_evidence_count;
            }
            break;
        case RequirementKind::Assertion:
            if (requirement.assertion) {
                const auto operation = assertion_operation(
                    requirement.assertion->operand_type);
                if (!operation.empty()) add_unique(query.operations, operation);
                query.comparison_strictness = merge_strictness(
                    std::move(query.comparison_strictness),
                    assertion_strictness(requirement.assertion->op));
            } else {
                ++query.unknown_evidence_count;
            }
            break;
        case RequirementKind::Calibration:
            if (requirement.calibration) {
                add_unique(query.operations, "derive_calibration");
                query.calibration = true;
            } else {
                ++query.unknown_evidence_count;
            }
            break;
        }
    }

    // Requirements retain source order for ordered sweeps. Support assessment
    // fills only dimensions that the stricter requirement extractor did not
    // project (for example, a sweep without explicit numeric bounds).
    for (const auto& clause : support.clauses) {
        if (clause.status != SupportClauseStatus::Supported) continue;
        if (clause.operation == "sweep.frequency" ||
            clause.operation == "fixed.frequency") {
            add_unique(query.operations, "sweep_frequency");
            add_unique(query.sweep_dimensions, "frequency");
            query.fixed_frequency = query.fixed_frequency ||
                                    clause.operation == "fixed.frequency";
        } else if (clause.operation == "sweep.temperature") {
            add_unique(query.operations, "sweep_temperature");
            add_unique(query.sweep_dimensions, "temperature");
        }
    }

    const auto text = normalized_text(instruction);
    if (text.find("calibrat") != std::string::npos ||
        text.find("correction table") != std::string::npos ||
        text.find("calibration table") != std::string::npos) {
        add_unique(query.operations, "derive_calibration");
        query.calibration = true;
    }
    if (text.find("power error") != std::string::npos ||
        text.find("power delta") != std::string::npos ||
        text.find("measured minus reference") != std::string::npos) {
        add_unique(query.operations, "derive_power_error");
    }
    if ((text.find("negat") != std::string::npos ||
         text.find("invert") != std::string::npos) &&
        (text.find("error") != std::string::npos ||
         text.find("delta") != std::string::npos)) {
        add_unique(query.operations, "derive_power_delta");
    }
    const bool strict_comparison = text.find(" below ") != std::string::npos ||
                                   text.find(" less than ") != std::string::npos ||
                                   text.find(" above ") != std::string::npos ||
                                   text.find(" greater than ") != std::string::npos ||
                                   text.find(" strictly ") != std::string::npos;
    const bool inclusive_comparison = text.find(" within ") != std::string::npos ||
                                      text.find(" at most ") != std::string::npos ||
                                      text.find(" at least ") != std::string::npos ||
                                      text.find(" no more than ") != std::string::npos ||
                                      text.find(" no less than ") != std::string::npos;
    if (strict_comparison || inclusive_comparison) {
        std::string projected_strictness = strict_comparison ? "strict" : "inclusive";
        if (strict_comparison && inclusive_comparison) projected_strictness = "mixed";
        query.comparison_strictness = merge_strictness(
            std::move(query.comparison_strictness), projected_strictness);
        if (text.find("evm") != std::string::npos ||
            text.find("quality") != std::string::npos) {
            add_unique(query.operations, "assert_evm");
        }
        if (text.find("frequency error") != std::string::npos ||
            text.find("carrier error") != std::string::npos ||
            text.find("carrier offset") != std::string::npos) {
            add_unique(query.operations, "assert_frequency_error");
        }
        if (text.find("power error") != std::string::npos ||
            text.find("power delta") != std::string::npos) {
            add_unique(query.operations, "assert_power_delta");
        } else if (text.find("power") != std::string::npos) {
            add_unique(query.operations, "assert_power");
        }
    }
    query.named_values = query.named_values || text.find(" named ") != std::string::npos ||
                         text.starts_with("named ");
    query.dependency_chain = text.find("then derive") != std::string::npos ||
                             text.find("from that") != std::string::npos ||
                             text.find("two stage") != std::string::npos ||
                             (contains(query.operations, "derive_power_error") &&
                              contains(query.operations, "derive_power_delta"));

    const auto derived_count = std::ranges::count_if(query.operations,
        [](const auto& operation) { return operation.starts_with("derive_"); });
    if (query.dependency_chain || derived_count >= 2) {
        query.complexity = RetrievalComplexity::Advanced;
    } else if (query.sweep_dimensions.size() >= 2U || query.operations.size() >= 6U ||
               query.comparison_strictness == "mixed") {
        query.complexity = RetrievalComplexity::Intermediate;
    }
    return query;
}

ExampleRetriever::ExampleRetriever(RetrievalCorpus corpus)
    : corpus_(std::move(corpus)) {}

RetrievalResult ExampleRetriever::retrieve(std::string_view instruction,
                                           RetrievalSettings settings) const {
    const auto started = std::chrono::steady_clock::now();
    RetrievalResult result;
    result.corpus_id = corpus_.corpus_id;
    result.corpus_version = corpus_.corpus_version;
    result.corpus_content_digest = corpus_.content_digest;
    result.ranking_id = std::string{retrieval_ranking_id()};
    result.query = analyze_retrieval_query(instruction);
    const auto finish = [&] {
        result.duration = std::chrono::steady_clock::now() - started;
        return result;
    };
    if (instruction.find_first_not_of(" \t\r\n") == std::string_view::npos) {
        result.diagnostics.push_back({RetrievalDiagnosticCode::EmptyInstruction,
                                      "retrieval instruction must not be empty"});
        return finish();
    }
    if (settings.top_k < 1U || settings.top_k > 3U) {
        result.diagnostics.push_back({RetrievalDiagnosticCode::InvalidTopK,
                                      "retrieval top_k must be 1, 2, or 3"});
        return finish();
    }
    if (corpus_.examples.empty()) {
        result.diagnostics.push_back({RetrievalDiagnosticCode::EmptyCorpus,
                                      "retrieval corpus contains no examples"});
        return finish();
    }
    if (result.query.operations.empty() && result.query.sweep_dimensions.empty()) {
        result.diagnostics.push_back({RetrievalDiagnosticCode::NoStructuralEvidence,
                                      "instruction contains no supported structural retrieval evidence"});
        return finish();
    }

    std::vector<RankedExample> ranked;
    ranked.reserve(corpus_.examples.size());
    for (std::size_t index = 0; index < corpus_.examples.size(); ++index) {
        ranked.push_back(rank_example(result.query, corpus_.examples[index], index));
    }
    std::ranges::stable_sort(ranked, [](const auto& left, const auto& right) {
        if (left.scores.total != right.scores.total) {
            return left.scores.total > right.scores.total;
        }
        return left.manifest_index < right.manifest_index;
    });

    std::size_t used_budget{};
    std::vector<std::size_t> selected_indexes;
    for (const auto& candidate : ranked) {
        if (result.selected.size() >= settings.top_k) break;
        if (candidate.scores.total <= 0.0) continue;
        const auto& example = corpus_.examples[candidate.manifest_index];
        const bool duplicate = std::ranges::any_of(selected_indexes,
            [&](std::size_t selected) {
                return near_duplicate(corpus_.examples[selected], example);
            });
        if (duplicate) {
            result.suppressed_near_duplicates.push_back(example.metadata.id);
            continue;
        }
        const auto bytes = context_bytes(example);
        const auto tokens = estimated_tokens(bytes);
        const auto cost = settings.budget_unit == RetrievalBudgetUnit::Utf8Bytes
                              ? bytes : tokens;
        if (cost > settings.context_budget -
                       std::min(settings.context_budget, used_budget)) {
            result.rejected_by_budget.push_back(example.metadata.id);
            continue;
        }
        used_budget += cost;
        selected_indexes.push_back(candidate.manifest_index);
        result.inserted_context_bytes += bytes;
        result.inserted_context_estimated_tokens += tokens;
        result.selected.push_back({example.metadata.id, candidate.scores,
                                   candidate.rationale, bytes, tokens});
    }
    if (result.selected.empty()) {
        result.diagnostics.push_back({RetrievalDiagnosticCode::NoMatchingExample,
                                      "no matching retrieval example fits the context budget"});
    }
    return finish();
}

RetrievalCorpusResult load_retrieval_corpus(
    const std::filesystem::path& root,
    const std::optional<std::filesystem::path>& comparison_dataset_root) {
    RetrievalCorpusResult result;
    Manifest manifest;
    const auto manifest_path = root / "manifest.json";
    try { manifest = parse_manifest(read_utf8_file(manifest_path)); }
    catch (const std::exception& error) {
        diagnostic(result, manifest_path, error.what());
        return result;
    }

    RetrievalCorpus corpus{manifest.corpus_id, manifest.version, manifest.digest, {}, {}};
    std::set<std::string, std::less<>> listed;
    std::set<std::string, std::less<>> identifiers;
    for (const auto& entry : manifest.examples) {
        if (!listed.insert(entry).second) {
            diagnostic(result, manifest_path, "duplicate example directory '" + entry + "'");
            continue;
        }
        const auto directory = root / "examples" / entry;
        const auto metadata_path = directory / "metadata.json";
        RetrievalExampleMetadata metadata;
        try { metadata = parse_metadata(read_utf8_file(metadata_path)); }
        catch (const std::exception& error) {
            diagnostic(result, metadata_path, error.what());
            continue;
        }
        if (metadata.id != entry) {
            diagnostic(result, metadata_path, "example id '" + metadata.id +
                       "' does not match directory name '" + entry + "'");
        }
        if (!identifiers.insert(metadata.id).second) {
            diagnostic(result, metadata_path, "duplicate example id '" + metadata.id + "'");
        }
        std::string request;
        std::string program;
        for (const auto& [filename, destination] :
             {std::pair{"request.txt", &request}, std::pair{"program.hrf", &program}}) {
            const auto path = directory / filename;
            try {
                *destination = read_utf8_file(path);
                if (destination->find_first_not_of(" \t\r\n") == std::string::npos) {
                    throw std::runtime_error("file must not be empty");
                }
            } catch (const std::exception& error) { diagnostic(result, path, error.what()); }
        }
        if (!program.empty()) {
            const auto validation = validate_script(program);
            if (!validation.ok()) {
                diagnostic(result, directory / "program.hrf",
                           "retrieval program failed compiler validation: " +
                           validation_summary(validation));
            }
        }
        corpus.examples.push_back({std::move(metadata), std::move(request),
                                   std::move(program), directory});
    }
    const auto examples_path = root / "examples";
    try {
        for (const auto& entry : std::filesystem::directory_iterator(examples_path)) {
            if (entry.is_directory() && !listed.contains(entry.path().filename().string())) {
                diagnostic(result, entry.path(), "example directory is not listed in manifest");
            }
        }
    } catch (const std::exception& error) { diagnostic(result, examples_path, error.what()); }

    try {
        const auto actual = digest_files(root, manifest);
        if (actual != manifest.digest) {
            diagnostic(result, manifest_path, "content_digest mismatch: expected '" +
                       manifest.digest + "', computed '" + actual + "'");
        }
    } catch (const std::exception& error) { diagnostic(result, root, error.what()); }

    for (std::size_t left = 0; left < corpus.examples.size(); ++left) {
        for (std::size_t right = left + 1; right < corpus.examples.size(); ++right) {
            const auto& other = corpus.examples[right];
            add_leakage(corpus.leakage_report, corpus.examples[left],
                        "corpus:" + other.metadata.id, other.request, other.program);
        }
    }

    if (comparison_dataset_root) {
        const auto comparison = load_golden_dataset(*comparison_dataset_root);
        if (!comparison.ok()) {
            diagnostic(result, *comparison_dataset_root,
                       "cannot load declared comparison dataset");
        } else {
            corpus.leakage_report.comparison_dataset_id = comparison.dataset->dataset_version;
            for (const auto& example : corpus.examples) {
                for (const auto& test_case : comparison.dataset->cases) {
                    add_leakage(corpus.leakage_report, example, test_case.metadata.id,
                                test_case.prompt, test_case.expected_source);
                }
            }
            std::ranges::sort(corpus.leakage_report.findings, {}, [](const LeakageFinding& item) {
                return item.corpus_example_id + "\n" + item.comparison_id + "\n" +
                       std::to_string(static_cast<int>(item.kind));
            });
        }
    }
    std::ranges::sort(result.diagnostics, {}, [](const RetrievalCorpusDiagnostic& item) {
        return item.path.generic_string() + "\n" + item.message;
    });
    if (result.diagnostics.empty()) result.corpus = std::move(corpus);
    return result;
}

std::string retrieval_corpus_content_digest(const std::filesystem::path& root) {
    const auto manifest = parse_manifest(read_utf8_file(root / "manifest.json"));
    return digest_files(root, manifest);
}

void write_leakage_report(const std::filesystem::path& path,
                          const RetrievalCorpus& corpus) {
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("cannot open leakage report");
    output.imbue(std::locale::classic());
    output << "{\"schema\":\"horusrf.retrieval-leakage-report\",\"version\":1"
           << ",\"corpus_id\":" << json_string(corpus.corpus_id)
           << ",\"corpus_version\":" << json_string(corpus.corpus_version)
           << ",\"content_digest\":" << json_string(corpus.content_digest)
           << ",\"comparison_dataset_id\":"
           << json_string(corpus.leakage_report.comparison_dataset_id)
           << ",\"findings\":[";
    for (std::size_t index = 0; index < corpus.leakage_report.findings.size(); ++index) {
        if (index != 0) output << ',';
        const auto& finding = corpus.leakage_report.findings[index];
        output << "{\"corpus_example_id\":" << json_string(finding.corpus_example_id)
               << ",\"comparison_id\":" << json_string(finding.comparison_id)
               << ",\"kind\":" << json_string(kind_name(finding.kind))
               << ",\"similarity\":" << std::fixed << std::setprecision(6)
               << finding.similarity << '}';
    }
    output << "]}\n";
    if (!output) throw std::runtime_error("cannot write leakage report");
}

} // namespace horusrf::ai
