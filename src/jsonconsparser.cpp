#include "jsonconsparser.h"

#if HAVE_JSONCONS

#include <cmath>
#include <climits>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <jsoncons/json_options.hpp>
#include <jsoncons/json_exception.hpp>
#include <jsoncons/json_reader.hpp>
#include <jsoncons/json_visitor.hpp>

struct jsoncons_parser {
    unsigned int max_depth = 500;
    unsigned int max_args = 500;
    bool silence = false;
};

namespace {

class UnicodeError final : public std::invalid_argument {
  public:
    using std::invalid_argument::invalid_argument;
};

// jsoncons 1.3.2 silently drops lone escaped low surrogates and does not check
// that the second code unit of a pair is a low surrogate. Validate just this
// lexical boundary while forwarding its public stream source, including pairs
// split over buffers. All JSON grammar and raw UTF-8 validation stay in jsoncons.
class UnicodeSource {
    enum class State { outside, text, escape, hex, pair_slash, pair_u };
    jsoncons::stream_source<char> source_;
    State state_ = State::outside;
    unsigned int codepoint_ = 0;
    int digits_ = 0;
    bool low_required_ = false;
    std::size_t line_ = 1;
    std::size_t column_ = 0;

    [[noreturn]] void invalid() const {
        throw UnicodeError("Invalid Unicode surrogate escape at line " +
            std::to_string(line_) + ", column " + std::to_string(column_));
    }

    void check(unsigned char c) {
        if (c == '\n') { ++line_; column_ = 0; } else { ++column_; }
        switch (state_) {
            case State::outside:
                if (c == '"') state_ = State::text;
                break;
            case State::text:
                if (c == '"') state_ = State::outside;
                else if (c == '\\') state_ = State::escape;
                break;
            case State::escape:
                if (c == 'u') {
                    state_ = State::hex;
                    codepoint_ = 0;
                    digits_ = 0;
                } else {
                    state_ = State::text;
                }
                break;
            case State::hex: {
                unsigned int digit;
                if (c >= '0' && c <= '9') digit = c - '0';
                else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                else invalid();
                codepoint_ = codepoint_ * 16 + digit;
                if (++digits_ != 4) break;
                if (low_required_) {
                    if (codepoint_ < 0xdc00 || codepoint_ > 0xdfff) invalid();
                    low_required_ = false;
                    state_ = State::text;
                } else if (codepoint_ >= 0xd800 && codepoint_ <= 0xdbff) {
                    low_required_ = true;
                    state_ = State::pair_slash;
                } else {
                    if (codepoint_ >= 0xdc00 && codepoint_ <= 0xdfff) invalid();
                    state_ = State::text;
                }
                break;
            }
            case State::pair_slash:
                if (c != '\\') invalid();
                state_ = State::pair_u;
                break;
            case State::pair_u:
                if (c != 'u') invalid();
                state_ = State::hex;
                codepoint_ = 0;
                digits_ = 0;
                break;
        }
    }

  public:
    using value_type = char;
    explicit UnicodeSource(std::istream& input) : source_(input) {}
    UnicodeSource(UnicodeSource&&) = default;
    bool eof() const { return source_.eof(); }
    bool is_error() const { return source_.is_error(); }
    jsoncons::span<const char> read_buffer() {
        const auto data = source_.read_buffer();
        for (const unsigned char c : data) check(c);
        return data;
    }
};

// jsoncons materializes one decoded token before delivering its event. Bound
// its temporary allocations as well as the visitor lengths, so an unterminated
// multi-megabyte string/number cannot consume document-sized memory first.
// Leave room for geometric growth and the parser's structural state stack.
template <typename T>
class TemporaryAllocator {
  public:
    using value_type = T;
    TemporaryAllocator() = default;
    template <typename U> TemporaryAllocator(const TemporaryAllocator<U>&) {}
    T* allocate(std::size_t count) {
        constexpr std::size_t budget =
            static_cast<std::size_t>(JSON_STRING_SIZE) * 4 + 16384;
        if (count > budget / sizeof(T))
            throw std::length_error("jsoncons token/state allocation limit exceeded");
        return std::allocator<T>{}.allocate(count);
    }
    void deallocate(T* data, std::size_t count) noexcept {
        std::allocator<T>{}.deallocate(data, count);
    }
    template <typename U> bool operator==(const TemporaryAllocator<U>&) const { return true; }
    template <typename U> bool operator!=(const TemporaryAllocator<U>&) const { return false; }
};

// Keep structural boundaries explicitly so dots, empty keys and NULs remain
// key data while argument names follow the historical dotted convention.
class ArgumentVisitor final : public jsoncons::default_json_visitor {
    struct Frame {
        std::size_t prefix_length;
        std::string key;
    };

    const jsoncons_parser& settings_;
    std::vector<Frame> stack_;
    std::string prefix_;
    std::string key_;
    unsigned int arguments_ = 0;
    const char* error_ = nullptr;

    void fail(const char* message, std::error_code& ec) {
        error_ = message;
        // Visitor return values alone do not abort jsoncons::json_reader.
        ec = std::make_error_code(std::errc::invalid_argument);
    }

    void begin(bool array, std::error_code& ec) {
        if (stack_.size() >= settings_.max_depth) {
            fail("JSON depth limit exceeded", ec);
            return;
        }
        const bool root_array = array && stack_.empty();
        std::size_t length = prefix_.size() + key_.size();
        if (!prefix_.empty()) ++length;
        if (root_array) length = 5;
        if (length >= JSON_STRING_SIZE) {
            fail("Argument name too long", ec);
            return;
        }
        stack_.push_back({prefix_.size(), key_});
        if (root_array) {
            prefix_ = "array";
            key_ = "array";
        } else {
            if (!prefix_.empty()) prefix_ += '.';
            prefix_ += key_;
        }
    }

    void end() {
        // The JSON reader guarantees properly paired begin/end events.
        prefix_.resize(stack_.back().prefix_length);
        key_ = std::move(stack_.back().key);
        stack_.pop_back();
    }

    void argument(const char* value, std::size_t length, std::error_code& ec) {
        if (arguments_ >= settings_.max_args) {
            fail("Argument number limit exceeded", ec);
            return;
        }
        const std::size_t name_length = prefix_.size() +
            (prefix_.empty() ? 0 : 1) + key_.size();
        // Silent parsing must enforce the same validation and size limits.
        if (name_length >= JSON_STRING_SIZE) {
            fail("Argument name too long", ec);
            return;
        }
        if (length >= JSON_STRING_SIZE) {
            fail("Argument value too long", ec);
            return;
        }
        ++arguments_;
        if (settings_.silence) return;

        // Length-aware writes preserve decoded embedded NULs and Unicode.
        bool ok = true;
        if (!prefix_.empty()) {
            ok = std::fwrite(prefix_.data(), 1, prefix_.size(), stdout) == prefix_.size();
            ok = (std::fputc('.', stdout) != EOF) && ok;
        }
        ok = (std::fwrite(key_.data(), 1, key_.size(), stdout) == key_.size()) && ok;
        ok = (std::fwrite(": ", 1, 2, stdout) == 2) && ok;
        ok = (std::fwrite(value, 1, length, stdout) == length) && ok;
        ok = (std::fputc('\n', stdout) != EOF) && ok;
        if (!ok) fail("Unable to write JSON argument output", ec);
    }

    JSONCONS_VISITOR_RETURN_TYPE visit_begin_object(jsoncons::semantic_tag,
            const jsoncons::ser_context&, std::error_code& ec) override {
        begin(false, ec);
        JSONCONS_VISITOR_RETURN;
    }
    JSONCONS_VISITOR_RETURN_TYPE visit_begin_array(jsoncons::semantic_tag,
            const jsoncons::ser_context&, std::error_code& ec) override {
        begin(true, ec);
        JSONCONS_VISITOR_RETURN;
    }
    JSONCONS_VISITOR_RETURN_TYPE visit_end_object(const jsoncons::ser_context&,
            std::error_code&) override {
        end();
        JSONCONS_VISITOR_RETURN;
    }
    JSONCONS_VISITOR_RETURN_TYPE visit_end_array(const jsoncons::ser_context&,
            std::error_code&) override {
        end();
        JSONCONS_VISITOR_RETURN;
    }
    JSONCONS_VISITOR_RETURN_TYPE visit_key(const string_view_type& name,
            const jsoncons::ser_context&, std::error_code& ec) override {
        if (name.size() >= JSON_STRING_SIZE) {
            fail("Argument name too long", ec);
        } else {
            key_.assign(name.data(), name.size());
        }
        JSONCONS_VISITOR_RETURN;
    }
    JSONCONS_VISITOR_RETURN_TYPE visit_null(jsoncons::semantic_tag,
            const jsoncons::ser_context&, std::error_code& ec) override {
        argument("", 0, ec);
        JSONCONS_VISITOR_RETURN;
    }
    JSONCONS_VISITOR_RETURN_TYPE visit_bool(bool value, jsoncons::semantic_tag,
            const jsoncons::ser_context&, std::error_code& ec) override {
        argument(value ? "true" : "false", value ? 4 : 5, ec);
        JSONCONS_VISITOR_RETURN;
    }
    JSONCONS_VISITOR_RETURN_TYPE visit_string(const string_view_type& value,
            jsoncons::semantic_tag, const jsoncons::ser_context&,
            std::error_code& ec) override {
        // lossless_number delivers decimals and oversized integers as tagged
        // strings, so no conversion through double (or truncation) is needed.
        argument(value.data(), value.size(), ec);
        JSONCONS_VISITOR_RETURN;
    }
    JSONCONS_VISITOR_RETURN_TYPE visit_int64(int64_t value, jsoncons::semantic_tag,
            const jsoncons::ser_context&, std::error_code& ec) override {
        const std::string text = std::to_string(value);
        argument(text.data(), text.size(), ec);
        JSONCONS_VISITOR_RETURN;
    }
    JSONCONS_VISITOR_RETURN_TYPE visit_uint64(uint64_t value, jsoncons::semantic_tag,
            const jsoncons::ser_context&, std::error_code& ec) override {
        const std::string text = std::to_string(value);
        argument(text.data(), text.size(), ec);
        JSONCONS_VISITOR_RETURN;
    }
    JSONCONS_VISITOR_RETURN_TYPE visit_double(double, jsoncons::semantic_tag,
            const jsoncons::ser_context&, std::error_code& ec) override {
        // Defensive: read options must prevent binary floating conversion.
        fail("Unexpected non-lossless JSON number event", ec);
        JSONCONS_VISITOR_RETURN;
    }

  public:
    explicit ArgumentVisitor(const jsoncons_parser& settings) : settings_(settings) {}
    const char* error() const { return error_; }
};

int set_limit(jsoncons_parser* parser, double value, unsigned int* target) {
    if (parser == nullptr || !std::isfinite(value) || value < 0 ||
        value > std::numeric_limits<unsigned int>::max() || std::floor(value) != value) {
        return -1;
    }
    *target = static_cast<unsigned int>(value);
    return 0;
}

} // namespace

extern "C" int jsoncons_json_init(jsoncons_parser** parser, char** error_msg) {
    if (error_msg == nullptr) return -1;
    *error_msg = nullptr;
    if (parser == nullptr) return -1;
    *parser = nullptr;
    try {
        *parser = std::make_unique<jsoncons_parser>().release();
        return 0;
    } catch (const std::bad_alloc& e) {
        *error_msg = strdup(e.what());
        return 1;
    } catch (...) {
        *error_msg = strdup("Unable to initialize jsoncons parser");
        return 1;
    }
}

extern "C" int jsoncons_parse_file(jsoncons_parser* parser, const char* filename,
        char** error_msg) {
    if (error_msg == nullptr) return -1;
    *error_msg = nullptr;
    if (parser == nullptr || filename == nullptr) return -1;

    // Keep all potentially throwing stream/library/visitor operations inside
    // the C boundary; stream, reader and visitor release storage on all exits.
    try {
        std::ifstream input(filename, std::ios::binary);
        if (!input.is_open()) {
            *error_msg = strdup("Unable to open JSON file");
            return 2;
        }
        ArgumentVisitor visitor(*parser);
        jsoncons::json_options options;
        options.lossless_number(true).err_handler(jsoncons::strict_json_parsing());
        // jsoncons uses int for its own nesting limit. For larger requested
        // values its representable ceiling is an additional safety limit.
        options.max_nesting_depth(static_cast<int>(
            parser->max_depth > static_cast<unsigned int>(INT_MAX) ?
            INT_MAX : parser->max_depth));
        jsoncons::basic_json_reader<char, UnicodeSource,
            TemporaryAllocator<char>> reader(input, visitor, options);
        std::error_code ec;
        reader.read(ec); // Includes finalization and checks trailing content.
        if (ec) {
            std::string message = visitor.error() ? visitor.error() : ec.message();
            message += " at line " + std::to_string(reader.line()) +
                       ", column " + std::to_string(reader.column());
            *error_msg = strdup(message.c_str());
            return 2;
        }
        if (input.bad()) {
            *error_msg = strdup("Error reading JSON file");
            return 2;
        }
        if (!parser->silence && std::fflush(stdout) != 0) {
            *error_msg = strdup("Unable to write JSON argument output");
            return 2;
        }
        return 0;
    } catch (const jsoncons::json_exception& e) {
        *error_msg = strdup(e.what());
        return 2;
    } catch (const std::exception& e) {
        *error_msg = strdup(e.what());
        return 2;
    } catch (...) {
        *error_msg = strdup("Unexpected jsoncons parser failure");
        return 2;
    }
}

extern "C" int jsoncons_set_max_depth(jsoncons_parser* parser, double max_depth) {
    return set_limit(parser, max_depth, parser ? &parser->max_depth : nullptr);
}
extern "C" int jsoncons_set_max_arg_num(jsoncons_parser* parser, double max_args) {
    return set_limit(parser, max_args, parser ? &parser->max_args : nullptr);
}
extern "C" int jsoncons_set_silence(jsoncons_parser* parser, int silence) {
    if (parser == nullptr) return -1;
    parser->silence = silence != 0;
    return 0;
}
extern "C" int jsoncons_json_cleanup(jsoncons_parser* parser) {
    const std::unique_ptr<jsoncons_parser> owned(parser);
    return 0;
}

#endif
