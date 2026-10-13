#include "jsoncppparser.h"

#if HAVE_JSONCPP

#include "cpparguments.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <json/json.h>
#include <memory>
#include <stdexcept>
#include <string>

struct jsoncpp_parser {
    unsigned int max_depth = 500;
    unsigned int max_args = 500;
    bool silence = false;
};

namespace {

class ParserError final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

bool valid_limit(double value) {
    return std::isfinite(value) && value >= 0 && value <= UINT_MAX &&
        std::floor(value) == value;
}

int hex_digit(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

struct Utf8Sequence {
    unsigned int code;
    unsigned int continuations;
    unsigned int minimum;
};

Utf8Sequence utf8_sequence(std::byte lead) {
    if (lead >= std::byte{0xc2} && lead <= std::byte{0xdf})
        return {std::to_integer<unsigned int>(lead & std::byte{0x1f}), 1, 0x80};
    if (lead >= std::byte{0xe0} && lead <= std::byte{0xef})
        return {std::to_integer<unsigned int>(lead & std::byte{0x0f}), 2, 0x800};
    if (lead >= std::byte{0xf0} && lead <= std::byte{0xf4})
        return {std::to_integer<unsigned int>(lead & std::byte{0x07}), 3, 0x10000};
    throw std::invalid_argument("Invalid UTF-8 in JSON input");
}

std::size_t utf8_end(const std::string& text, std::size_t start) {
    auto sequence = utf8_sequence(static_cast<std::byte>(text[start]));
    if (sequence.continuations > text.size() - start - 1)
        throw std::invalid_argument("Truncated UTF-8 in JSON input");
    for (unsigned int n = 1; n <= sequence.continuations; ++n) {
        const auto next = static_cast<std::byte>(text[start + n]);
        if ((next & std::byte{0xc0}) != std::byte{0x80})
            throw std::invalid_argument("Invalid UTF-8 in JSON input");
        sequence.code = (sequence.code << 6) |
            std::to_integer<unsigned int>(next & std::byte{0x3f});
    }
    if (sequence.code < sequence.minimum || sequence.code > 0x10ffff ||
        (sequence.code >= 0xd800 && sequence.code <= 0xdfff))
        throw std::invalid_argument("Invalid UTF-8 code point in JSON input");
    return start + sequence.continuations + 1;
}

unsigned int unicode_unit(const std::string& text, std::size_t start) {
    if (text.size() - start < 4)
        throw std::invalid_argument("Truncated Unicode escape");
    unsigned int code = 0;
    for (unsigned int n = 0; n < 4; ++n) {
        const int digit = hex_digit(static_cast<unsigned char>(text[start + n]));
        if (digit < 0) throw std::invalid_argument("Invalid Unicode escape");
        code = (code << 4) | static_cast<unsigned int>(digit);
    }
    return code;
}

std::size_t escape_end(const std::string& text, std::size_t start) {
    if (text.size() - start == 1) return text.size(); // Reader diagnoses truncation.
    if (text[start + 1] != 'u') return start + 2;
    const unsigned int first = unicode_unit(text, start + 2);
    const std::size_t end = start + 6;
    if (first >= 0xdc00 && first <= 0xdfff)
        throw std::invalid_argument("Unpaired low Unicode surrogate");
    if (first < 0xd800 || first > 0xdbff) return end;
    if (text.size() - end < 6 || text[end] != '\\' || text[end + 1] != 'u')
        throw std::invalid_argument("Unpaired high Unicode surrogate");
    if (const unsigned int second = unicode_unit(text, end + 2);
        second < 0xdc00 || second > 0xdfff)
        throw std::invalid_argument("Invalid Unicode surrogate pair");
    return end + 6;
}

// JsonCpp accepts raw invalid UTF-8 and lone low surrogate escapes. Validate
// these lexical boundaries before constructing its native DOM. The library
// remains responsible for document grammar and decoded values.
void validate_encoding(const std::string& text) {
    bool in_string = false;
    std::size_t position = 0;
    while (position < text.size()) {
        const char c = text[position];
        if (c == '\0') throw std::invalid_argument("Unexpected NUL in JSON input");
        if (in_string && static_cast<unsigned char>(c) < 0x20)
            throw std::invalid_argument("Unescaped control character in JSON string");
        if (static_cast<std::byte>(c) >= std::byte{0x80}) {
            position = utf8_end(text, position);
            continue;
        }
        if (c == '"') in_string = !in_string;
        if (in_string && c == '\\') {
            position = escape_end(text, position);
            continue;
        }
        ++position;
    }
}

bool decimal_digit(char c) {
    return c >= '0' && c <= '9';
}

std::size_t digits_end(const std::string& token, std::size_t position) {
    while (position < token.size() && decimal_digit(token[position])) ++position;
    return position;
}

bool required_digits(const std::string& token, std::size_t& position) {
    const std::size_t end = digits_end(token, position);
    const bool present = end != position;
    position = end;
    return present;
}

bool valid_number(const std::string& token) {
    std::size_t i = 0;
    if (i < token.size() && token[i] == '-') ++i;
    if (i == token.size()) return false;
    if (token[i] == '0') ++i;
    else {
        if (token[i] < '1' || token[i] > '9') return false;
        i = digits_end(token, i);
    }
    if (i < token.size() && token[i] == '.') {
        ++i;
        if (!required_digits(token, i)) return false;
    }
    if (i < token.size() && (token[i] == 'e' || token[i] == 'E')) {
        ++i;
        if (i < token.size() && (token[i] == '+' || token[i] == '-')) ++i;
        if (!required_digits(token, i)) return false;
    }
    return i == token.size();
}

class Arguments {
    const jsoncpp_parser& settings_;
    const std::string& input_;
    PathHandler path_;
    unsigned int count_ = 0;

    void check_path() const {
        if (path_.m_error) throw std::length_error("Argument name too long");
    }

    void emit(const std::string& value) {
        jsonbench::emit_argument<ParserError>(count_, settings_.max_args,
            settings_.silence, path_, value);
    }

    void visit_members(const Json::Value& value, unsigned int depth) {
        // JsonCpp stores objects in key order. Source offsets restore input
        // order for unique keys; duplicate keys are rejected by the reader.
        auto names = value.getMemberNames();
        std::sort(names.begin(), names.end(), [&value](const std::string& a, const std::string& b) {
            return value[a].getOffsetStart() < value[b].getOffsetStart();
        });
        for (const auto& key : names) {
            path_.setKey(key.data(), key.size());
            check_path();
            visit(value[key], depth + 1);
        }
    }

    void visit_container(const Json::Value& value, unsigned int depth) {
        if (depth >= settings_.max_depth)
            throw std::length_error("JSON depth limit exceeded");
        if (value.isArray()) path_.startArraySpecial();
        else path_.push();
        check_path();
        if (value.isArray()) {
            for (const auto& child : value) visit(child, depth + 1);
        } else visit_members(value, depth);
        path_.pop();
    }

    void visit_number(const Json::Value& value) {
        const auto start = value.getOffsetStart();
        const auto end = value.getOffsetLimit();
        if (start < 0 || end < start || static_cast<std::size_t>(end) > input_.size())
            throw ParserError("Invalid JsonCpp numeric source offsets");
        const std::string token = input_.substr(static_cast<std::size_t>(start),
            static_cast<std::size_t>(end - start));
        if (!valid_number(token)) throw std::invalid_argument("Invalid JSON number");
        // Preserve the validated spelling, including integers beyond 64 bits,
        // rather than converting its DOM double back to text.
        emit(token);
    }

    void visit_scalar(const Json::Value& value) {
        switch (value.type()) {
            case Json::nullValue: emit(""); break;
            case Json::booleanValue: emit(value.asBool() ? "true" : "false"); break;
            case Json::stringValue: emit(value.asString()); break;
            case Json::intValue:
            case Json::uintValue:
            case Json::realValue: visit_number(value); break;
            default: throw std::invalid_argument("Unsupported JSON value");
        }
    }

  public:
    Arguments(const jsoncpp_parser& settings, const std::string& input)
        : settings_(settings), input_(input) {}

    void visit(const Json::Value& value, unsigned int depth = 0) {
        if (value.isObject() || value.isArray()) visit_container(value, depth);
        else visit_scalar(value);
    }
};

} // namespace

extern "C" int jsoncpp_json_init(jsoncpp_parser **parser, char **error_msg) {
    if (!parser || !error_msg) return -1;
    *parser = nullptr;
    *error_msg = nullptr;
    try { *parser = std::make_unique<jsoncpp_parser>().release(); return 0; }
    catch (const std::exception& e) { *error_msg = strdup(e.what()); return 1; }
    catch (...) { *error_msg = strdup("Unable to initialize JsonCpp parser"); return 1; }
}

extern "C" int jsoncpp_parse_file(jsoncpp_parser *parser, const char *filename, char **error_msg) {
    if (error_msg) *error_msg = nullptr;
    if (!parser || !filename || !error_msg) return -1;
    try {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) throw ParserError("Unable to open JSON file");
        const std::string input{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        if (file.bad()) throw ParserError("Unable to read JSON file");
        validate_encoding(input);
        Json::CharReaderBuilder builder;
        builder["collectComments"] = false;
        builder["allowComments"] = false;
        builder["allowTrailingCommas"] = false;
        builder["strictRoot"] = false;
        builder["allowDroppedNullPlaceholders"] = false;
        builder["allowNumericKeys"] = false;
        builder["allowSingleQuotes"] = false;
        builder["failIfExtra"] = true;
        builder["rejectDupKeys"] = true;
        builder["allowSpecialFloats"] = false;
        builder["skipBom"] = false;
        // Bound native recursive parsing even if a caller requests UINT_MAX.
        // Traversal enforces the exact configured container depth below.
        builder["stackLimit"] = static_cast<Json::UInt>(
            std::min<unsigned int>(parser->max_depth, 1023) + 1);
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        Json::Value root;
        std::string errors;
        if (!reader->parse(input.data(), input.data() + input.size(), &root, &errors))
            throw std::invalid_argument(errors);
        Arguments(*parser, input).visit(root);
        if (!parser->silence && std::fflush(stdout) == EOF)
            throw ParserError("Unable to write JSON argument output");
        return 0;
    } catch (const std::exception& e) {
        *error_msg = strdup(e.what());
        return 2;
    } catch (...) {
        *error_msg = strdup("JsonCpp parsing failed");
        return 2;
    }
}

extern "C" int jsoncpp_set_max_depth(jsoncpp_parser *parser, double value) {
    if (!parser || !valid_limit(value)) return -1;
    parser->max_depth = static_cast<unsigned int>(value);
    return 0;
}

extern "C" int jsoncpp_set_max_arg_num(jsoncpp_parser *parser, double value) {
    if (!parser || !valid_limit(value)) return -1;
    parser->max_args = static_cast<unsigned int>(value);
    return 0;
}

extern "C" int jsoncpp_set_silence(jsoncpp_parser *parser, int silence) {
    if (!parser) return -1;
    parser->silence = silence != 0;
    return 0;
}

extern "C" int jsoncpp_json_cleanup(jsoncpp_parser *parser) {
    const std::unique_ptr<jsoncpp_parser> owned(parser);
    return 0;
}

#endif
