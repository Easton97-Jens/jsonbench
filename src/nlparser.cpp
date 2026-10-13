#include "nlparser.h"

#if HAVE_NLOHMANNJSON

#include "pathhandler.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <istream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>

struct nl_parser {
    unsigned int max_depth = 500;
    unsigned int max_args = 500;
    bool silence = false;
};

namespace {

using nljson = nlohmann::json;

// The public SAX API still materializes individual strings and number tokens.
// Guard their raw size before the lexer can allocate an unbounded token. Six
// source bytes per decoded byte allow legal \uXXXX escapes at the value limit.
// Reject raw NUL as well: nlohmann 3.11.3 otherwise treats it as end of input.
class CheckedInputBuffer final : public std::streambuf {
    std::FILE* file_ = nullptr;
    const char* memory_ = nullptr;
    std::size_t remaining_ = 0;
    std::array<char, 16384> buffer_{};
    bool in_string_ = false;
    bool escaped_ = false;
    bool in_number_ = false;
    std::size_t token_length_ = 0;

    static bool number_character(unsigned char c) {
        return (c >= '0' && c <= '9') || c == '-' || c == '+' ||
            c == '.' || c == 'e' || c == 'E';
    }

    void check(unsigned char c) {
        if (c == 0) throw std::invalid_argument("Invalid raw NUL in JSON input");
        if (in_string_) {
            if (!escaped_ && c == '"') {
                in_string_ = false;
                token_length_ = 0;
                return;
            }
            if (++token_length_ > static_cast<std::size_t>(JSON_STRING_SIZE) * 6)
                throw std::length_error("JSON string token too long");
            if (escaped_) escaped_ = false;
            else if (c == '\\') escaped_ = true;
            return;
        }
        if (in_number_) {
            if (number_character(c)) {
                if (++token_length_ >= JSON_STRING_SIZE)
                    throw std::length_error("JSON number token too long");
                return;
            }
            in_number_ = false;
            token_length_ = 0;
        }
        if (c == '"') {
            in_string_ = true;
            escaped_ = false;
        } else if ((c >= '0' && c <= '9') || c == '-') {
            in_number_ = true;
            token_length_ = 1;
        }
    }

    int_type underflow() override {
        std::size_t count;
        if (file_) {
            count = std::fread(buffer_.data(), 1, buffer_.size(), file_);
            if (std::ferror(file_)) throw std::runtime_error("Error reading JSON file");
        } else {
            count = remaining_ < buffer_.size() ? remaining_ : buffer_.size();
            if (count != 0) std::memcpy(buffer_.data(), memory_, count);
            memory_ += count;
            remaining_ -= count;
        }
        if (count == 0) return traits_type::eof();
        for (std::size_t i = 0; i < count; ++i)
            check(static_cast<unsigned char>(buffer_[i]));
        setg(buffer_.data(), buffer_.data(), buffer_.data() + count);
        return traits_type::to_int_type(*gptr());
    }

  public:
    explicit CheckedInputBuffer(std::FILE* file) : file_(file) {}
    CheckedInputBuffer(const char* memory, std::size_t length)
        : memory_(memory), remaining_(length) {}
};

class NLSAXHandler final : public nlohmann::json_sax<nljson> {
    const nl_parser& settings_;
    unsigned int depth_ = 0;
    unsigned int arguments_ = 0;
    PathHandler path_;
    std::string error_;

    bool fail(const char* message) {
        error_ = message;
        return false;
    }

    bool begin(bool array) {
        if (depth_ >= settings_.max_depth) return fail("JSON depth limit exceeded");
        if (array) path_.startArraySpecial();
        else path_.push();
        if (path_.m_error) return fail("Argument name too long");
        ++depth_;
        return true;
    }

    bool end() {
        if (depth_ == 0) return fail("Invalid JSON container state");
        path_.pop();
        --depth_;
        if (path_.m_error) return fail("Invalid JSON argument path");
        return true;
    }

    bool argument(std::string_view value) {
        if (arguments_ >= settings_.max_args)
            return fail("Argument number limit exceeded");
        const std::string name = path_.name();
        if (path_.m_error) return fail("Argument name too long");
        if (value.size() >= JSON_STRING_SIZE) return fail("Argument value too long");
        // Unlimited argument mode must not wrap the counter on enormous input.
        ++arguments_;
        if (settings_.silence) return true;
        if (std::fwrite(name.data(), 1, name.size(), stdout) != name.size() ||
            std::fwrite(": ", 1, 2, stdout) != 2 ||
            std::fwrite(value.data(), 1, value.size(), stdout) != value.size() ||
            std::fputc('\n', stdout) == EOF)
            return fail("Unable to write JSON argument output");
        return true;
    }

  public:
    explicit NLSAXHandler(const nl_parser& settings) : settings_(settings) {}
    const std::string& error() const { return error_; }

    bool start_object(std::size_t) override { return begin(false); }
    bool end_object() override { return end(); }
    bool start_array(std::size_t) override { return begin(true); }
    bool end_array() override { return end(); }
    bool key(nljson::string_t& value) override {
        path_.setKey(value.data(), value.size());
        return !path_.m_error || fail("Argument name too long");
    }
    bool string(nljson::string_t& value) override { return argument(value); }
    bool number_integer(nljson::number_integer_t value) override {
        // nlohmann's integer event has no original token; this normalizes -0.
        return argument(std::to_string(value));
    }
    bool number_unsigned(nljson::number_unsigned_t value) override {
        return argument(std::to_string(value));
    }
    bool number_float(nljson::number_float_t, const nljson::string_t& token) override {
        // Preserve the lexer spelling, including integers outside uint64/int64,
        // instead of emitting the rounded double. Nonfinite overflow is rejected
        // by nlohmann before this event and remains an explicit parse failure.
        return argument(token);
    }
    bool null() override { return argument(""); }
    bool boolean(bool value) override { return argument(value ? "true" : "false"); }
    bool binary(nljson::binary_t&) override { return fail("Unexpected binary JSON event"); }
    bool parse_error(std::size_t, const std::string&,
                     const nlohmann::detail::exception& error) override {
        error_ = error.what();
        return false;
    }
};

int fail(char** error_msg, const char* message, int status = 2) {
    if (error_msg) *error_msg = ::strdup(message);
    return status;
}

bool valid_limit(double value) {
    return std::isfinite(value) && value >= 0 && std::floor(value) == value &&
        value <= std::numeric_limits<unsigned int>::max();
}

struct FileCloser {
    void operator()(std::FILE* file) const noexcept { std::fclose(file); }
};

int parse(nl_parser* parser, CheckedInputBuffer& source, char** error_msg) {
    try {
        // A fresh visitor keeps failed or completed documents from carrying
        // depth, argument counts, errors or paths into a subsequent parse.
        NLSAXHandler handler(*parser);
        std::istream input(&source);
        const bool ok = nljson::sax_parse(input, &handler,
            nljson::input_format_t::json, true, false);
        if (!ok) return fail(error_msg, handler.error().empty() ?
            "NlohmannJSON parsing failed" : handler.error().c_str());
        if (!parser->silence && std::fflush(stdout) != 0)
            return fail(error_msg, "Unable to write JSON argument output");
        return 0;
    } catch (const std::exception& error) {
        return fail(error_msg, error.what());
    } catch (...) {
        return fail(error_msg, "Unexpected NlohmannJSON parser failure");
    }
}

} // namespace

extern "C" int nl_json_init(nl_parser** parser, char** error_msg) {
    if (error_msg) *error_msg = nullptr;
    if (parser) *parser = nullptr;
    if (!parser || !error_msg) return -1;
    try {
        *parser = new nl_parser;
        return 0;
    } catch (const std::exception& error) {
        return fail(error_msg, error.what(), -1);
    } catch (...) {
        return fail(error_msg, "Unable to initialize NlohmannJSON parser", -1);
    }
}

extern "C" int nl_json_cleanup(nl_parser* parser) {
    delete parser;
    return 0;
}

extern "C" int nl_parse_buffer(nl_parser* parser, const char* buffer,
                              unsigned int length, char** error_msg) {
    if (error_msg) *error_msg = nullptr;
    if (!parser || !buffer || !error_msg)
        return fail(error_msg, "Invalid NlohmannJSON parser arguments", -1);
    try {
        CheckedInputBuffer source(buffer, length);
        return parse(parser, source, error_msg);
    } catch (const std::exception& error) {
        return fail(error_msg, error.what());
    } catch (...) {
        return fail(error_msg, "Unexpected NlohmannJSON parser failure");
    }
}

extern "C" int nl_parse_file(nl_parser* parser, const char* filename, char** error_msg) {
    if (error_msg) *error_msg = nullptr;
    if (!parser || !filename || !error_msg)
        return fail(error_msg, "Invalid NlohmannJSON parser arguments", -1);
    try {
        std::unique_ptr<std::FILE, FileCloser> file(std::fopen(filename, "rb"));
        if (!file) return fail(error_msg, "Unable to open JSON file", -1);
        CheckedInputBuffer source(file.get());
        return parse(parser, source, error_msg);
    } catch (const std::exception& error) {
        return fail(error_msg, error.what());
    } catch (...) {
        return fail(error_msg, "Unexpected NlohmannJSON parser failure");
    }
}

extern "C" int nl_set_max_depth(nl_parser* parser, double value) {
    if (!parser || !valid_limit(value)) return -1;
    parser->max_depth = static_cast<unsigned int>(value);
    return 0;
}

extern "C" int nl_set_max_arg_num(nl_parser* parser, double value) {
    if (!parser || !valid_limit(value)) return -1;
    parser->max_args = static_cast<unsigned int>(value);
    return 0;
}

extern "C" int nl_set_silence(nl_parser* parser, int silence) {
    if (!parser) return -1;
    parser->silence = silence != 0;
    return 0;
}

// Retain this old, undeclared symbol without manually destroying an object
// that nl_json_cleanup would subsequently destroy a second time.
extern "C" int nl_parser_cleanup(nl_parser* parser) {
    return parser ? 0 : -1;
}

#endif
