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
#include "horusrf/ai/requirement_verification.hpp"

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

double require_number(const JsonObject& object, std::string_view name) {
    const auto* value = std::get_if<double>(&require_member(object, name).value);
    if (value == nullptr) {
        throw std::runtime_error("member '" + std::string{name} + "' must be a number");
    }
    return *value;
}

bool optional_bool(const JsonObject& object, std::string_view name, bool fallback) {
    const auto found = object.find(name);
    if (found == object.end()) return fallback;
    const auto* value = std::get_if<bool>(&found->second.value);
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

struct RequirementSelector final {
    std::string id;
    RequirementKind kind{RequirementKind::ReferencePower};
    std::optional<ir::SweepDimension> dimension;
    std::optional<ir::MeasurementOperation> operation;
    std::optional<std::size_t> ordinal;
    bool constrain_step{true};
    bool constrain_order{true};
    bool require_name{false};
};

struct ParsedMetadata final {
    GoldenCaseMetadata metadata;
    std::vector<RequirementSelector> selectors;
};

RequirementKind requirement_kind(std::string_view value) {
    if (value == "reference_power") return RequirementKind::ReferencePower;
    if (value == "sweep") return RequirementKind::Sweep;
    if (value == "measurement") return RequirementKind::Measurement;
    if (value == "assertion") return RequirementKind::Assertion;
    if (value == "calibration") return RequirementKind::Calibration;
    throw std::runtime_error("unsupported requirement kind '" + std::string{value} + "'");
}

std::vector<RequirementSelector> parse_requirement_selectors(const JsonObject& object) {
    const auto* array = std::get_if<JsonArray>(&require_member(object, "requirements").value);
    if (array == nullptr || array->empty()) {
        throw std::runtime_error("member 'requirements' must be a non-empty array");
    }
    std::vector<RequirementSelector> result;
    std::set<std::string, std::less<>> identifiers;
    for (const auto& item : *array) {
        const auto& requirement = require_object(item, "requirement");
        const auto kind = requirement_kind(require_string(requirement, "kind"));
        RequirementSelector selector{require_string(requirement, "id"), kind};
        if (!identifiers.insert(selector.id).second) {
            throw std::runtime_error("duplicate requirement id '" + selector.id + "'");
        }
        switch (kind) {
        case RequirementKind::ReferencePower:
            require_exact_members(requirement, {"id", "kind"});
            break;
        case RequirementKind::Sweep: {
            require_exact_members(requirement, {"id", "kind", "dimension",
                                                "constrain_step", "constrain_order"});
            const auto dimension = require_string(requirement, "dimension");
            if (dimension == "frequency") selector.dimension = ir::SweepDimension::Frequency;
            else if (dimension == "temperature") selector.dimension = ir::SweepDimension::Temperature;
            else throw std::runtime_error("unsupported sweep requirement dimension '" + dimension + "'");
            selector.constrain_step = optional_bool(requirement, "constrain_step", true);
            selector.constrain_order = optional_bool(requirement, "constrain_order", true);
            break;
        }
        case RequirementKind::Measurement: {
            require_exact_members(requirement, {"id", "kind", "operation", "require_name"});
            const auto operation = require_string(requirement, "operation");
            if (operation == "power") selector.operation = ir::MeasurementOperation::Power;
            else if (operation == "evm") selector.operation = ir::MeasurementOperation::Evm;
            else if (operation == "frequency_error") selector.operation = ir::MeasurementOperation::FrequencyError;
            else throw std::runtime_error("unsupported measurement requirement operation '" + operation + "'");
            selector.require_name = optional_bool(requirement, "require_name", false);
            break;
        }
        case RequirementKind::Assertion: {
            require_exact_members(requirement, {"id", "kind", "ordinal"});
            const auto ordinal = require_number(requirement, "ordinal");
            if (ordinal < 0 || ordinal != static_cast<double>(static_cast<std::size_t>(ordinal)))
                throw std::runtime_error("assertion requirement ordinal must be a non-negative integer");
            selector.ordinal = static_cast<std::size_t>(ordinal);
            break;
        }
        case RequirementKind::Calibration:
            require_exact_members(requirement, {"id", "kind"});
            break;
        }
        result.push_back(std::move(selector));
    }
    return result;
}

ParsedMetadata parse_metadata(const std::string& source) {
    const auto object = require_object(JsonParser{source}.parse(), "metadata");
    const auto schema = object.find("schema");
    const bool version_two = schema != object.end();
    std::size_t schema_version = 1;
    if (version_two) {
        const auto version = require_number(object, "version");
        if (require_string(object, "schema") != "horusrf.golden-case" ||
            (version != 2.0 && version != 3.0)) {
            throw std::runtime_error("unsupported golden case schema version");
        }
        schema_version = static_cast<std::size_t>(version);
        if (schema_version == 2) {
            require_exact_members(object, {"schema", "version", "id", "description",
                                           "category", "difficulty", "tags",
                                           "expected_valid", "comparison",
                                           "required_capabilities", "requirements"});
        } else {
            require_exact_members(object, {"schema", "version", "id", "description",
                                           "category", "difficulty", "tags",
                                           "expected_valid", "expected_support", "comparison",
                                           "required_capabilities", "requirements"});
        }
    } else {
        require_exact_members(object, {"id", "description", "category", "difficulty",
                                       "tags", "expected_valid", "comparison",
                                       "required_capabilities"});
    }
    GoldenCaseMetadata metadata{
        require_string(object, "id"),
        require_string(object, "description"),
        require_string(object, "category"),
        require_string(object, "difficulty"),
        require_string_array(object, "tags"),
        require_bool(object, "expected_valid"),
        require_string(object, "comparison"),
        require_string_array(object, "required_capabilities"),
        schema_version,
        {},
    };
    if (schema_version == 3) {
        const auto expected_support = require_string(object, "expected_support");
        if (expected_support == "supported") {
            metadata.expected_support = SupportAssessmentStatus::Supported;
        } else if (expected_support == "partially_supported") {
            metadata.expected_support = SupportAssessmentStatus::PartiallySupported;
        } else if (expected_support == "unsupported") {
            metadata.expected_support = SupportAssessmentStatus::Unsupported;
        } else if (expected_support == "uncertain") {
            metadata.expected_support = SupportAssessmentStatus::Uncertain;
        } else {
            throw std::runtime_error("unsupported expected_support '" + expected_support + "'");
        }
    } else {
        metadata.expected_support = metadata.expected_valid
                                        ? SupportAssessmentStatus::Supported
                                        : SupportAssessmentStatus::Unsupported;
    }
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
    if (schema_version == 3) {
        const bool expects_program =
            metadata.expected_support == SupportAssessmentStatus::Supported ||
            metadata.expected_support == SupportAssessmentStatus::PartiallySupported;
        if (metadata.expected_valid != expects_program) {
            throw std::runtime_error(
                "expected_valid must be true for supported or partially supported "
                "cases and false for unsupported or uncertain cases");
        }
    }
    std::vector<RequirementSelector> selectors;
    if (version_two && metadata.expected_valid) {
        selectors = parse_requirement_selectors(object);
    } else if (version_two) {
        const auto* requirements = std::get_if<JsonArray>(&require_member(object, "requirements").value);
        if (requirements == nullptr || !requirements->empty()) {
            throw std::runtime_error("unsupported cases must have an empty requirements array");
        }
    }
    return {std::move(metadata), std::move(selectors)};
}

std::vector<RequirementClause> materialize_requirements(
    const std::vector<RequirementSelector>& selectors, const ProgramFacts& facts) {
    std::vector<RequirementClause> result;
    for (const auto& selector : selectors) {
        RequirementClause clause;
        clause.id = selector.id;
        clause.kind = selector.kind;
        clause.constrain_sweep_step = selector.constrain_step;
        clause.constrain_sweep_order = selector.constrain_order;
        clause.constrain_measurement_name = selector.require_name;
        switch (selector.kind) {
        case RequirementKind::ReferencePower: clause.reference_power = facts.reference_power; break;
        case RequirementKind::Sweep: {
            const auto found = std::ranges::find(facts.sweeps, *selector.dimension,
                                                 &SweepFact::dimension);
            if (found == facts.sweeps.end()) throw std::runtime_error("requirement selects a missing sweep");
            clause.sweep = *found;
            break;
        }
        case RequirementKind::Measurement: {
            const auto found = std::ranges::find(facts.measurements, *selector.operation,
                                                 &MeasurementFact::operation);
            if (found == facts.measurements.end()) throw std::runtime_error("requirement selects a missing measurement");
            clause.measurement = *found;
            break;
        }
        case RequirementKind::Assertion: {
            const auto found = std::ranges::find(facts.assertions, *selector.ordinal,
                                                 &AssertionFact::ordinal);
            if (found == facts.assertions.end()) throw std::runtime_error("requirement selects a missing assertion");
            clause.assertion = *found;
            break;
        }
        case RequirementKind::Calibration: clause.calibration = facts.calibration; break;
        }
        result.push_back(std::move(clause));
    }
    return result;
}

struct Manifest final {
    std::string dataset_version;
    std::vector<std::string> cases;
    std::optional<std::string> baseline_dataset_version;
    std::size_t baseline_case_count{};
};

Manifest parse_manifest(const std::string& source) {
    const auto object = require_object(JsonParser{source}.parse(), "manifest");
    if (require_string(object, "schema") != "horusrf.golden-dataset") {
        throw std::runtime_error("unsupported manifest schema");
    }
    const auto* version = std::get_if<double>(&require_member(object, "version").value);
    if (version == nullptr || (*version != 1.0 && *version != 2.0)) {
        throw std::runtime_error("unsupported manifest version");
    }
    if (*version == 1.0) {
        require_exact_members(object, {"schema", "version", "dataset_version", "cases"});
        return {require_string(object, "dataset_version"),
                require_string_array(object, "cases"), std::nullopt, 0};
    }
    require_exact_members(object, {"schema", "version", "dataset_version",
                                   "baseline_dataset_version", "baseline_case_count",
                                   "cases"});
    const auto baseline_count = require_number(object, "baseline_case_count");
    if (baseline_count <= 0.0 ||
        baseline_count != static_cast<double>(static_cast<std::size_t>(baseline_count))) {
        throw std::runtime_error("baseline_case_count must be a positive integer");
    }
    return {require_string(object, "dataset_version"),
            require_string_array(object, "cases"),
            require_string(object, "baseline_dataset_version"),
            static_cast<std::size_t>(baseline_count)};
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

    GoldenDataset dataset{manifest.dataset_version, {},
                          manifest.baseline_dataset_version,
                          manifest.baseline_case_count};
    if (dataset.baseline_case_count > manifest.cases.size()) {
        add_diagnostic(result, manifest_path,
                       "baseline_case_count exceeds the number of manifest cases");
    }
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
        std::vector<RequirementSelector> selectors;
        try {
            auto parsed = parse_metadata(read_file(metadata_path));
            metadata = std::move(parsed.metadata);
            selectors = std::move(parsed.selectors);
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
            if (metadata.expected_valid && validation.ok() && !selectors.empty()) {
                const auto facts = compile_program_facts(expected_source);
                if (!facts) {
                    add_diagnostic(result, expected_path,
                                   "expected-valid script could not be projected to program facts");
                } else {
                    try {
                        metadata.requirements = materialize_requirements(selectors, *facts);
                    } catch (const std::exception& error) {
                        add_diagnostic(result, metadata_path, error.what());
                    }
                }
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
