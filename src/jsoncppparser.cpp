#include "jsoncppparser.h"

#if HAVE_JSONCPP

#include "pathhandler.h"
#include <algorithm>
#include <climits>
#include <cmath>
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

// JsonCpp accepts raw invalid UTF-8 and lone low surrogate escapes. Validate
// these lexical boundaries before constructing its native DOM. The library
// remains responsible for document grammar and decoded values.
void validate_encoding(const std::string& text) {
    bool in_string = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == 0) throw std::invalid_argument("Unexpected NUL in JSON input");
        if (in_string && c < 0x20)
            throw std::invalid_argument("Unescaped control character in JSON string");
        if (c >= 0x80) {
            unsigned int code = 0, count = 0, minimum = 0;
            if (c >= 0xc2 && c <= 0xdf) { code = c & 0x1f; count = 1; minimum = 0x80; }
            else if (c >= 0xe0 && c <= 0xef) { code = c & 0x0f; count = 2; minimum = 0x800; }
            else if (c >= 0xf0 && c <= 0xf4) { code = c & 0x07; count = 3; minimum = 0x10000; }
            else throw std::invalid_argument("Invalid UTF-8 in JSON input");
            if (count > text.size() - i - 1)
                throw std::invalid_argument("Truncated UTF-8 in JSON input");
            for (unsigned int n = 0; n < count; ++n) {
                const unsigned char next = static_cast<unsigned char>(text[++i]);
                if ((next & 0xc0) != 0x80)
                    throw std::invalid_argument("Invalid UTF-8 in JSON input");
                code = (code << 6) | (next & 0x3f);
            }
            if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
                throw std::invalid_argument("Invalid UTF-8 code point in JSON input");
            continue;
        }
        if (c == '"') { in_string = !in_string; continue; }
        if (!in_string || c != '\\') continue;
        if (++i == text.size()) break; // The reader diagnoses truncation.
        if (text[i] != 'u') continue;
        auto unit = [&text](std::size_t at) {
            if (text.size() - at < 4)
                throw std::invalid_argument("Truncated Unicode escape");
            unsigned int code = 0;
            for (unsigned int n = 0; n < 4; ++n) {
                int digit = hex_digit(static_cast<unsigned char>(text[at + n]));
                if (digit < 0) throw std::invalid_argument("Invalid Unicode escape");
                code = (code << 4) | static_cast<unsigned int>(digit);
            }
            return code;
        };
        const unsigned int first = unit(i + 1);
        i += 4;
        if (first >= 0xdc00 && first <= 0xdfff)
            throw std::invalid_argument("Unpaired low Unicode surrogate");
        if (first < 0xd800 || first > 0xdbff) continue;
        if (text.size() - i - 1 < 6 || text[i + 1] != '\\' || text[i + 2] != 'u')
            throw std::invalid_argument("Unpaired high Unicode surrogate");
        const unsigned int second = unit(i + 3);
        if (second < 0xdc00 || second > 0xdfff)
            throw std::invalid_argument("Invalid Unicode surrogate pair");
        i += 6;
    }
}

bool valid_number(const std::string& token) {
    std::size_t i = 0;
    if (i < token.size() && token[i] == '-') ++i;
    if (i == token.size()) return false;
    if (token[i] == '0') ++i;
    else {
        if (token[i] < '1' || token[i] > '9') return false;
        while (i < token.size() && token[i] >= '0' && token[i] <= '9') ++i;
    }
    if (i < token.size() && token[i] == '.') {
        const std::size_t start = ++i;
        while (i < token.size() && token[i] >= '0' && token[i] <= '9') ++i;
        if (i == start) return false;
    }
    if (i < token.size() && (token[i] == 'e' || token[i] == 'E')) {
        ++i;
        if (i < token.size() && (token[i] == '+' || token[i] == '-')) ++i;
        const std::size_t start = i;
        while (i < token.size() && token[i] >= '0' && token[i] <= '9') ++i;
        if (i == start) return false;
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
        const std::string name = path_.name();
        check_path();
        if (value.size() >= JSON_STRING_SIZE)
            throw std::length_error("Argument value too long");
        if (count_ >= settings_.max_args)
            throw std::length_error("Argument number limit exceeded");
        ++count_;
        if (settings_.silence) return;
        if (std::fwrite(name.data(), 1, name.size(), stdout) != name.size() ||
            std::fwrite(": ", 1, 2, stdout) != 2 ||
            std::fwrite(value.data(), 1, value.size(), stdout) != value.size() ||
            std::fputc('\n', stdout) == EOF)
            throw std::runtime_error("Unable to write JSON argument output");
    }

  public:
    Arguments(const jsoncpp_parser& settings, const std::string& input)
        : settings_(settings), input_(input) {}

    void visit(const Json::Value& value, unsigned int depth = 0) {
        if (value.isObject() || value.isArray()) {
            if (depth >= settings_.max_depth)
                throw std::length_error("JSON depth limit exceeded");
            if (value.isArray()) path_.startArraySpecial();
            else path_.push();
            check_path();
            if (value.isArray()) {
                for (const auto& child : value) visit(child, depth + 1);
            } else {
                // JsonCpp stores objects in key order. Source offsets restore
                // input order for unique keys; duplicate keys are rejected.
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
            path_.pop();
            return;
        }
        if (value.isNull()) emit("");
        else if (value.isBool()) emit(value.asBool() ? "true" : "false");
        else if (value.isString()) emit(value.asString());
        else if (value.isNumeric()) {
            const auto start = value.getOffsetStart();
            const auto end = value.getOffsetLimit();
            if (start < 0 || end < start || static_cast<std::size_t>(end) > input_.size())
                throw std::runtime_error("Invalid JsonCpp numeric source offsets");
            const std::string token = input_.substr(static_cast<std::size_t>(start),
                static_cast<std::size_t>(end - start));
            if (!valid_number(token)) throw std::invalid_argument("Invalid JSON number");
            // Preserve the validated spelling, including integers beyond 64
            // bits, rather than converting its DOM double back to text.
            emit(token);
        } else throw std::invalid_argument("Unsupported JSON value");
    }
};

} // namespace

extern "C" int jsoncpp_json_init(jsoncpp_parser **parser, char **error_msg) {
    if (!parser || !error_msg) return -1;
    *parser = nullptr;
    *error_msg = nullptr;
    try { *parser = new jsoncpp_parser{}; return 0; }
    catch (const std::exception& e) { *error_msg = strdup(e.what()); return 1; }
    catch (...) { *error_msg = strdup("Unable to initialize JsonCpp parser"); return 1; }
}

extern "C" int jsoncpp_parse_file(jsoncpp_parser *parser, const char *filename, char **error_msg) {
    if (error_msg) *error_msg = nullptr;
    if (!parser || !filename || !error_msg) return -1;
    try {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) throw std::runtime_error("Unable to open JSON file");
        const std::string input{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        if (file.bad()) throw std::runtime_error("Unable to read JSON file");
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
            throw std::runtime_error("Unable to write JSON argument output");
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
    delete parser;
    return 0;
}

#endif
