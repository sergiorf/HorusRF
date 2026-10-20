#include "horusrf/ai/prompt.hpp"

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace horusrf::ai {
namespace {

struct JsonValue final {
    enum class Kind { Null, Boolean, Number, String, Array, Object };

    Kind kind{Kind::Null};
    std::string string;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue, std::less<>> object;
};

class JsonParser final {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    JsonValue parse() {
        skip_space();
        JsonValue result = parse_value();
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
               (input_[position_] == ' ' || input_[position_] == '\t' ||
                input_[position_] == '\r' || input_[position_] == '\n')) {
            ++position_;
        }
    }

    bool consume(char expected) {
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    JsonValue parse_value() {
        if (position_ >= input_.size()) fail("expected a JSON value");
        if (input_[position_] == '"') return make_string();
        if (input_[position_] == '[') return parse_array();
        if (input_[position_] == '{') return parse_object();
        if (input_.substr(position_, 4) == "null") return parse_literal("null", JsonValue::Kind::Null);
        if (input_.substr(position_, 4) == "true") return parse_literal("true", JsonValue::Kind::Boolean);
        if (input_.substr(position_, 5) == "false") return parse_literal("false", JsonValue::Kind::Boolean);
        return parse_number();
    }

    JsonValue parse_literal(std::string_view literal, JsonValue::Kind kind) {
        position_ += literal.size();
        JsonValue result;
        result.kind = kind;
        return result;
    }

    static void append_utf8(std::string& output, std::uint32_t codepoint) {
        if (codepoint <= 0x7f) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ff) {
            output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else if (codepoint <= 0xffff) {
            output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else {
            output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    }

    std::uint32_t parse_hex_quad() {
        if (position_ + 4 > input_.size()) fail("incomplete Unicode escape");
        std::uint32_t result{};
        for (int index = 0; index < 4; ++index) {
            const char digit = input_[position_++];
            result <<= 4;
            if (digit >= '0' && digit <= '9') result += static_cast<unsigned>(digit - '0');
            else if (digit >= 'a' && digit <= 'f') result += static_cast<unsigned>(digit - 'a' + 10);
            else if (digit >= 'A' && digit <= 'F') result += static_cast<unsigned>(digit - 'A' + 10);
            else fail("invalid Unicode escape");
        }
        return result;
    }

    std::string parse_string() {
        if (!consume('"')) fail("expected string");
        std::string output;
        while (position_ < input_.size()) {
            const unsigned char value = static_cast<unsigned char>(input_[position_++]);
            if (value == '"') return output;
            if (value < 0x20) fail("unescaped control character");
            if (value != '\\') {
                output.push_back(static_cast<char>(value));
                continue;
            }
            if (position_ >= input_.size()) fail("incomplete escape");
            switch (input_[position_++]) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u': {
                std::uint32_t codepoint = parse_hex_quad();
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (position_ + 2 > input_.size() || input_[position_] != '\\' ||
                        input_[position_ + 1] != 'u') {
                        fail("missing low surrogate");
                    }
                    position_ += 2;
                    const std::uint32_t low = parse_hex_quad();
                    if (low < 0xdc00 || low > 0xdfff) fail("invalid low surrogate");
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                    fail("unexpected low surrogate");
                }
                append_utf8(output, codepoint);
                break;
            }
            default: fail("invalid string escape");
            }
        }
        fail("unterminated string");
    }

    JsonValue make_string() {
        JsonValue result;
        result.kind = JsonValue::Kind::String;
        result.string = parse_string();
        return result;
    }

    JsonValue parse_number() {
        const std::size_t begin = position_;
        consume('-');
        if (consume('0')) {
            if (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                fail("leading zero in number");
            }
        } else {
            const std::size_t digits = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
            if (position_ == digits) fail("expected a JSON value");
        }
        if (consume('.')) {
            const std::size_t digits = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
            if (position_ == digits) fail("incomplete fraction");
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            const std::size_t digits = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
            if (position_ == digits) fail("incomplete exponent");
        }
        if (position_ == begin) fail("expected a JSON value");
        JsonValue result;
        result.kind = JsonValue::Kind::Number;
        return result;
    }

    JsonValue parse_array() {
        consume('[');
        JsonValue result;
        result.kind = JsonValue::Kind::Array;
        skip_space();
        if (consume(']')) return result;
        for (;;) {
            skip_space();
            result.array.push_back(parse_value());
            skip_space();
            if (consume(']')) return result;
            if (!consume(',')) fail("expected ',' or ']'");
        }
    }

    JsonValue parse_object() {
        consume('{');
        JsonValue result;
        result.kind = JsonValue::Kind::Object;
        skip_space();
        if (consume('}')) return result;
        for (;;) {
            skip_space();
            if (position_ >= input_.size() || input_[position_] != '"') fail("expected object key");
            std::string key = parse_string();
            skip_space();
            if (!consume(':')) fail("expected ':'");
            skip_space();
            const auto [unused, inserted] = result.object.emplace(std::move(key), parse_value());
            static_cast<void>(unused);
            if (!inserted) fail("duplicate object key");
            skip_space();
            if (consume('}')) return result;
            if (!consume(',')) fail("expected ',' or '}'");
        }
    }

    std::string_view input_;
    std::size_t position_{};
};

ProposalResponseResult schema_error(std::string message) {
    return ProposalResponseError{ProposalResponseErrorCode::SchemaViolation, std::move(message)};
}

} // namespace

ProposalResponseResult parse_proposal_response(std::string_view response) {
    JsonValue root;
    try {
        root = JsonParser{response}.parse();
    } catch (const std::exception& error) {
        return ProposalResponseError{ProposalResponseErrorCode::MalformedJson, error.what()};
    }

    if (root.kind != JsonValue::Kind::Object) {
        return schema_error("proposal response must be a JSON object");
    }
    if (root.object.size() != 4) {
        return schema_error("proposal response must contain exactly status, script, explanation, and warnings");
    }
    const auto status = root.object.find("status");
    const auto script = root.object.find("script");
    const auto explanation = root.object.find("explanation");
    const auto warnings = root.object.find("warnings");
    if (status == root.object.end() || status->second.kind != JsonValue::Kind::String ||
        status->second.string != "proposal") {
        return schema_error("status must be the string 'proposal'");
    }
    if (script == root.object.end() || script->second.kind != JsonValue::Kind::String) {
        return schema_error("script must be a string");
    }
    if (explanation == root.object.end() || explanation->second.kind != JsonValue::Kind::String) {
        return schema_error("explanation must be a string");
    }
    if (warnings == root.object.end() || warnings->second.kind != JsonValue::Kind::Array) {
        return schema_error("warnings must be an array");
    }
    std::vector<std::string> parsed_warnings;
    parsed_warnings.reserve(warnings->second.array.size());
    for (const auto& warning : warnings->second.array) {
        if (warning.kind != JsonValue::Kind::String) {
            return schema_error("every warning must be a string");
        }
        parsed_warnings.push_back(warning.string);
    }
    return ProposalPayload{ScriptSource{script->second.string}, explanation->second.string,
                           std::move(parsed_warnings)};
}

} // namespace horusrf::ai
