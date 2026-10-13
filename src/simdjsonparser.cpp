#include "simdjsonparser.h"

#if HAVE_SIMDJSON

#include "pathhandler.h"
#include <simdjson.h>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

struct simdjson_parser {
    unsigned int max_depth = 500;
    unsigned int max_args = 500;
    bool silence = false;
    simdjson::ondemand::parser parser;
};

namespace {

bool valid_limit(double value) {
    return std::isfinite(value) && value >= 0 && value <= UINT_MAX &&
        std::floor(value) == value;
}

void check(simdjson::error_code error) {
    if (error) throw std::invalid_argument(simdjson::error_message(error));
}

class Arguments {
    const simdjson_parser& settings_;
    PathHandler path_;
    unsigned int count_ = 0;

    void check_path() const {
        if (path_.m_error) throw std::length_error("Argument name too long");
    }

    void check_depth(unsigned int depth) const {
        if (depth >= settings_.max_depth)
            throw std::length_error("JSON depth limit exceeded");
        // Recursive visitor frames are larger in sanitizer builds. Keep an
        // independent adapter ceiling so raising the user limit cannot exhaust
        // the process stack before reaching the library's own state limit.
        if (depth >= 512)
            throw std::length_error("simdjson adapter JSON depth limit (512) exceeded");
        // On-Demand's parser has its own finite state stack. Calling get_array
        // or get_object beyond it can assert in development builds; stop before
        // accessing that stack even when the caller requests a larger limit.
        if (static_cast<std::size_t>(depth) + 1 >= settings_.parser.max_depth())
            throw std::length_error("simdjson native JSON depth limit exceeded");
    }

    void emit(std::string_view value) {
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
    explicit Arguments(const simdjson_parser& settings) : settings_(settings) {}

    // The document overload is essential for scalar roots: converting a root
    // document to ondemand::value only works for arrays/objects in simdjson.
    template <typename Value>
    void visit(Value& value, unsigned int depth = 0) {
        simdjson::ondemand::json_type type = simdjson::ondemand::json_type::null;
        check(value.type().get(type));
        switch (type) {
            case simdjson::ondemand::json_type::object: {
                check_depth(depth);
                path_.push();
                check_path();
                simdjson::ondemand::object object;
                check(value.get_object().get(object));
                for (auto field_result : object) {
                    simdjson::ondemand::field field;
                    check(std::move(field_result).get(field));
                    std::string_view key;
                    check(field.unescaped_key(false).get(key));
                    path_.setKey(key.data(), key.size());
                    check_path();
                    auto child = field.value();
                    visit(child, depth + 1);
                }
                path_.pop();
                break;
            }
            case simdjson::ondemand::json_type::array: {
                check_depth(depth);
                path_.startArraySpecial();
                check_path();
                simdjson::ondemand::array array;
                check(value.get_array().get(array));
                for (auto child_result : array) {
                    simdjson::ondemand::value child;
                    check(std::move(child_result).get(child));
                    visit(child, depth + 1);
                }
                path_.pop();
                break;
            }
            case simdjson::ondemand::json_type::number: {
                std::string_view raw;
                // Capturing the public raw token preserves integer precision
                // and decimal spelling; get_number still validates and consumes
                // every numeric value. Unsupported ranges fail explicitly.
                check(value.raw_json().get(raw));
                while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' ||
                    raw.back() == '\r' || raw.back() == '\n')) raw.remove_suffix(1);
                simdjson::ondemand::number number;
                check(value.get_number().get(number));
                emit(raw);
                break;
            }
            case simdjson::ondemand::json_type::string: {
                std::string_view text;
                check(value.get_string(false).get(text));
                emit(text);
                break;
            }
            case simdjson::ondemand::json_type::boolean: {
                bool flag = false;
                check(value.get_bool().get(flag));
                emit(flag ? "true" : "false");
                break;
            }
            case simdjson::ondemand::json_type::null: {
                bool is_null = false;
                check(value.is_null().get(is_null));
                if (!is_null) throw std::invalid_argument("Invalid JSON null");
                emit("");
                break;
            }
        }
    }
};

} // namespace

extern "C" int simdjson_json_init(simdjson_parser **parser, char **error_msg) {
    if (!parser || !error_msg) return -1;
    *parser = nullptr;
    *error_msg = nullptr;
    try { *parser = new simdjson_parser{}; return 0; }
    catch (const std::exception& e) { *error_msg = strdup(e.what()); return 1; }
    catch (...) { *error_msg = strdup("Unable to initialize simdjson parser"); return 1; }
}

extern "C" int simdjson_parse_file(simdjson_parser *parser, const char *filename, char **error_msg) {
    if (error_msg) *error_msg = nullptr;
    if (!parser || !filename || !error_msg) return -1;
    try {
        // On-Demand avoids a DOM but needs a complete padded input buffer.
        auto json_result = simdjson::padded_string::load(filename);
        check(json_result.error());
        auto& json = json_result.value_unsafe();
        auto result = parser->parser.iterate(json);
        check(result.error());
        auto& document = result.value_unsafe();
        Arguments(*parser).visit(document);
        if (!document.at_end())
            throw std::invalid_argument("Unexpected content after JSON document");
        if (!parser->silence && std::fflush(stdout) == EOF)
            throw std::runtime_error("Unable to write JSON argument output");
        return 0;
    } catch (const std::exception& e) {
        *error_msg = strdup(e.what());
        return 2;
    } catch (...) {
        *error_msg = strdup("simdjson parsing failed");
        return 2;
    }
}

extern "C" int simdjson_set_max_depth(simdjson_parser *parser, double value) {
    if (!parser || !valid_limit(value)) return -1;
    parser->max_depth = static_cast<unsigned int>(value);
    return 0;
}

extern "C" int simdjson_set_max_arg_num(simdjson_parser *parser, double value) {
    if (!parser || !valid_limit(value)) return -1;
    parser->max_args = static_cast<unsigned int>(value);
    return 0;
}

extern "C" int simdjson_set_silence(simdjson_parser *parser, int silence) {
    if (!parser) return -1;
    parser->silence = silence != 0;
    return 0;
}

extern "C" int simdjson_json_cleanup(simdjson_parser *parser) {
    delete parser;
    return 0;
}

#endif
