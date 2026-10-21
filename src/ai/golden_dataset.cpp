#include "horusrf/ai/golden_dataset.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <initializer_list>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

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
        auto result = parse_value();
        skip_space();
        if (position_ != input_.size()) fail("unexpected trailing content");
        return result;
    }

private:
    [[noreturn]] void fail(std::string_view message) const {
        throw std::runtime_error(std::string{message} + " at byte " +
                                 std::to_string(position_));
    }

    void skip_space() {
        while (position_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[position_])) != 0) {
            ++position_;
        }
    }

    bool consume(char expected) {
        skip_space();
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    void require(char expected) {
        if (!consume(expected)) {
            fail(std::string{"expected '"} + expected + "'");
        }
    }

    bool consume_literal(std::string_view literal) {
        skip_space();
        if (input_.substr(position_, literal.size()) != literal) return false;
        position_ += literal.size();
        return true;
    }

    JsonValue parse_value() {
        skip_space();
        if (position_ >= input_.size()) fail("expected a JSON value");
        switch (input_[position_]) {
        case '{': return JsonValue{parse_object()};
        case '[': return JsonValue{parse_array()};
        case '"': return JsonValue{parse_string()};
        case 't':
            if (consume_literal("true")) return JsonValue{true};
            break;
        case 'f':
            if (consume_literal("false")) return JsonValue{false};
            break;
        case 'n':
            if (consume_literal("null")) return JsonValue{nullptr};
            break;
        default:
            if (input_[position_] == '-' ||
                std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                return JsonValue{parse_number()};
            }
        }
        fail("invalid JSON value");
    }

    JsonObject parse_object() {
        require('{');
        JsonObject result;
        if (consume('}')) return result;
        while (true) {
            skip_space();
            if (position_ >= input_.size() || input_[position_] != '"') {
                fail("expected an object member name");
            }
            auto key = parse_string();
            require(':');
            auto [unused, inserted] = result.emplace(std::move(key), parse_value());
            static_cast<void>(unused);
            if (!inserted) fail("duplicate object member");
            if (consume('}')) return result;
            require(',');
        }
    }

    JsonArray parse_array() {
        require('[');
        JsonArray result;
        if (consume(']')) return result;
        while (true) {
            result.push_back(parse_value());
            if (consume(']')) return result;
            require(',');
        }
    }

    static unsigned hex_digit(char value) {
        if (value >= '0' && value <= '9') return static_cast<unsigned>(value - '0');
        if (value >= 'a' && value <= 'f') return 10U + static_cast<unsigned>(value - 'a');
        if (value >= 'A' && value <= 'F') return 10U + static_cast<unsigned>(value - 'A');
        throw std::runtime_error("invalid unicode escape");
    }

    void append_utf8(std::string& output, unsigned codepoint) const {
        if (codepoint <= 0x7fU) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ffU) {
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
        std::string result;
        while (position_ < input_.size()) {
            const char current = input_[position_++];
            if (current == '"') return result;
            if (static_cast<unsigned char>(current) < 0x20U) {
                fail("unescaped control character in string");
            }
            if (current != '\\') {
                result.push_back(current);
                continue;
            }
            if (position_ >= input_.size()) fail("incomplete string escape");
            const char escaped = input_[position_++];
            switch (escaped) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                if (position_ + 4U > input_.size()) fail("incomplete unicode escape");
                unsigned codepoint = 0;
                try {
                    for (int index = 0; index < 4; ++index) {
                        codepoint = (codepoint << 4U) | hex_digit(input_[position_++]);
                    }
                } catch (const std::runtime_error&) {
                    fail("invalid unicode escape");
                }
                if (codepoint >= 0xd800U && codepoint <= 0xdfffU) {
                    fail("unicode surrogate escapes are not supported");
                }
                append_utf8(result, codepoint);
                break;
            }
            default: fail("invalid string escape");
            }
        }
        fail("unterminated string");
    }

    double parse_number() {
        skip_space();
        const auto begin = position_;
        if (input_[position_] == '-') ++position_;
        if (position_ >= input_.size()) fail("incomplete number");
        if (input_[position_] == '0') {
            ++position_;
        } else if (input_[position_] >= '1' && input_[position_] <= '9') {
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                ++position_;
            }
        } else {
            fail("invalid number");
        }
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            if (position_ >= input_.size() ||
                std::isdigit(static_cast<unsigned char>(input_[position_])) == 0) {
                fail("invalid fractional number");
            }
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                ++position_;
            }
        }
        if (position_ < input_.size() &&
            (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() &&
                (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            if (position_ >= input_.size() ||
                std::isdigit(static_cast<unsigned char>(input_[position_])) == 0) {
                fail("invalid number exponent");
            }
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                ++position_;
            }
        }
        double value{};
        const auto text = input_.substr(begin, position_ - begin);
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
            fail("invalid number");
        }
        return value;
    }

    std::string_view input_;
    std::size_t position_{};
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open file");
    std::ostringstream content;
    content << input.rdbuf();
    if (!input.eof() && input.fail()) throw std::runtime_error("cannot read file");
    return content.str();
}

const JsonObject& require_object(const JsonValue& value, std::string_view context) {
    const auto* object = std::get_if<JsonObject>(&value.value);
    if (object == nullptr) throw std::runtime_error(std::string{context} + " must be an object");
    return *object;
}

const JsonValue& require_member(const JsonObject& object, std::string_view name) {
    const auto found = object.find(name);
    if (found == object.end()) {
        throw std::runtime_error("missing required member '" + std::string{name} + "'");
    }
    return found->second;
}

std::string require_string(const JsonObject& object, std::string_view name) {
    const auto* value = std::get_if<std::string>(&require_member(object, name).value);
    if (value == nullptr || value->empty()) {
        throw std::runtime_error("member '" + std::string{name} +
                                 "' must be a non-empty string");
    }
    return *value;
}

bool require_bool(const JsonObject& object, std::string_view name) {
    const auto* value = std::get_if<bool>(&require_member(object, name).value);
    if (value == nullptr) {
        throw std::runtime_error("member '" + std::string{name} + "' must be a boolean");
    }
    return *value;
}

std::vector<std::string> require_string_array(const JsonObject& object,
                                               std::string_view name,
                                               bool allow_empty = false) {
    const auto* array = std::get_if<JsonArray>(&require_member(object, name).value);
    if (array == nullptr || (!allow_empty && array->empty())) {
        throw std::runtime_error("member '" + std::string{name} +
                                 "' must be a non-empty string array");
    }
    std::vector<std::string> result;
    std::set<std::string, std::less<>> unique;
    for (const auto& item : *array) {
        const auto* value = std::get_if<std::string>(&item.value);
        if (value == nullptr || value->empty()) {
            throw std::runtime_error("member '" + std::string{name} +
                                     "' must contain non-empty strings");
        }
        if (!unique.insert(*value).second) {
            throw std::runtime_error("member '" + std::string{name} +
                                     "' contains duplicate value '" + *value + "'");
        }
        result.push_back(*value);
    }
    return result;
}

void require_exact_members(const JsonObject& object,
                           std::initializer_list<std::string_view> names) {
    std::set<std::string, std::less<>> expected;
    for (const auto name : names) expected.emplace(name);
    for (const auto& [name, unused] : object) {
        static_cast<void>(unused);
        if (!expected.contains(name)) {
            throw std::runtime_error("unknown member '" + name + "'");
        }
    }
}

GoldenCaseMetadata parse_metadata(const std::string& source) {
    const auto object = require_object(JsonParser{source}.parse(), "metadata");
    require_exact_members(object, {"id", "description", "category", "difficulty",
                                   "tags", "expected_valid", "comparison",
                                   "required_capabilities"});
    GoldenCaseMetadata metadata{
        require_string(object, "id"),
        require_string(object, "description"),
        require_string(object, "category"),
        require_string(object, "difficulty"),
        require_string_array(object, "tags"),
        require_bool(object, "expected_valid"),
        require_string(object, "comparison"),
        require_string_array(object, "required_capabilities"),
    };
    const std::set<std::string_view> difficulties{"basic", "intermediate", "advanced"};
    if (!difficulties.contains(metadata.difficulty)) {
        throw std::runtime_error("unsupported difficulty '" + metadata.difficulty + "'");
    }
    if (metadata.comparison != "semantic" && metadata.comparison != "unsupported") {
        throw std::runtime_error("unsupported comparison '" + metadata.comparison + "'");
    }
    if (metadata.expected_valid != (metadata.comparison == "semantic")) {
        throw std::runtime_error(
            "expected_valid must be true for semantic cases and false for unsupported cases");
    }
    return metadata;
}

struct Manifest final {
    std::string dataset_version;
    std::vector<std::string> cases;
};

Manifest parse_manifest(const std::string& source) {
    const auto object = require_object(JsonParser{source}.parse(), "manifest");
    require_exact_members(object, {"schema", "version", "dataset_version", "cases"});
    if (require_string(object, "schema") != "horusrf.golden-dataset") {
        throw std::runtime_error("unsupported manifest schema");
    }
    const auto* version = std::get_if<double>(&require_member(object, "version").value);
    if (version == nullptr || *version != 1.0) {
        throw std::runtime_error("unsupported manifest version");
    }
    return {require_string(object, "dataset_version"),
            require_string_array(object, "cases")};
}

void add_diagnostic(GoldenDatasetResult& result, const std::filesystem::path& path,
                    std::string message) {
    result.diagnostics.push_back({path, std::move(message)});
}

std::string validation_summary(const ValidationResult& validation) {
    if (validation.diagnostics().empty()) return "no compiler diagnostic";
    const auto& first = validation.diagnostics().front();
    return first.code + ": " + first.message;
}

} // namespace

GoldenDatasetResult load_golden_dataset(const std::filesystem::path& root) {
    GoldenDatasetResult result;
    const auto manifest_path = root / "manifest.json";
    Manifest manifest;
    try {
        manifest = parse_manifest(read_file(manifest_path));
    } catch (const std::exception& error) {
        add_diagnostic(result, manifest_path, error.what());
        return result;
    }

    GoldenDataset dataset{manifest.dataset_version, {}};
    std::set<std::string, std::less<>> identifiers;
    std::set<std::string, std::less<>> manifest_directories;
    for (const auto& entry : manifest.cases) {
        if (!manifest_directories.insert(entry).second) {
            add_diagnostic(result, manifest_path,
                           "duplicate case directory '" + entry + "'");
            continue;
        }
        const auto directory = root / "cases" / entry;
        const auto metadata_path = directory / "metadata.json";
        GoldenCaseMetadata metadata;
        try {
            metadata = parse_metadata(read_file(metadata_path));
        } catch (const std::exception& error) {
            add_diagnostic(result, metadata_path, error.what());
            continue;
        }
        if (metadata.id != entry) {
            add_diagnostic(result, metadata_path,
                           "case id '" + metadata.id +
                               "' does not match directory name '" + entry + "'");
        }
        if (!identifiers.insert(metadata.id).second) {
            add_diagnostic(result, metadata_path,
                           "duplicate case id '" + metadata.id + "'");
        }

        std::string prompt;
        std::string expected_source;
        const auto prompt_path = directory / "prompt.txt";
        const auto expected_path = directory / "expected.hrf";
        try {
            prompt = read_file(prompt_path);
            if (prompt.find_first_not_of(" \t\r\n") == std::string::npos) {
                throw std::runtime_error("prompt must not be empty");
            }
        } catch (const std::exception& error) {
            add_diagnostic(result, prompt_path, error.what());
        }
        try {
            expected_source = read_file(expected_path);
            if (expected_source.find_first_not_of(" \t\r\n") == std::string::npos) {
                throw std::runtime_error("expected source must not be empty");
            }
        } catch (const std::exception& error) {
            add_diagnostic(result, expected_path, error.what());
        }

        if (!expected_source.empty()) {
            const auto validation = validate_script(expected_source);
            if (metadata.expected_valid && !validation.ok()) {
                add_diagnostic(result, expected_path,
                               "expected-valid script failed compiler validation: " +
                                   validation_summary(validation));
            } else if (!metadata.expected_valid && validation.ok()) {
                add_diagnostic(result, expected_path,
                               "unsupported case unexpectedly contains a valid script");
            }
        }
        dataset.cases.push_back(
            {std::move(metadata), std::move(prompt), std::move(expected_source), directory});
    }

    const auto cases_path = root / "cases";
    try {
        for (const auto& entry : std::filesystem::directory_iterator(cases_path)) {
            if (entry.is_directory()) {
                const auto name = entry.path().filename().string();
                if (!manifest_directories.contains(name)) {
                    add_diagnostic(result, entry.path(),
                                   "case directory is not listed in manifest");
                }
            }
        }
    } catch (const std::exception& error) {
        add_diagnostic(result, cases_path, error.what());
    }

    std::ranges::sort(result.diagnostics, {}, [](const GoldenDatasetDiagnostic& diagnostic) {
        return diagnostic.path.generic_string() + "\n" + diagnostic.message;
    });
    if (result.diagnostics.empty()) result.dataset = std::move(dataset);
    return result;
}

} // namespace horusrf::ai
