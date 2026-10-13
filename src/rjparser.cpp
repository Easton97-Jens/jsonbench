#include "rjparser.h"

#if HAVE_RAPIDJSON

#include "pathhandler.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include <rapidjson/error/en.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/memorystream.h>
#include <rapidjson/reader.h>

struct rj_parser {
    unsigned int max_depth = 500;
    unsigned int max_args = 500;
    bool silence = false;
};

namespace {

// RapidJSON keeps one decoded token and its iterative structural stack here.
// A checked allocator stops hostile unterminated strings/numbers from growing
// document-sized storage before a SAX callback can enforce the token limit.
class TemporaryAllocator {
  public:
    static const bool kNeedFree = true;
    void* Malloc(std::size_t size) { return Realloc(nullptr, 0, size); }
    void* Realloc(void* data, std::size_t, std::size_t size) {
        constexpr std::size_t budget =
            static_cast<std::size_t>(JSON_STRING_SIZE) * 4 + 16384;
        if (size > budget)
            throw std::length_error("RapidJSON token/state allocation limit exceeded");
        if (size == 0) {
            std::free(data);
            return nullptr;
        }
        void* result = std::realloc(data, size);
        if (result == nullptr) throw std::bad_alloc();
        return result;
    }
    static void Free(void* data) { std::free(data); }
};

// RapidJSON uses zero both for its synthetic EOF sentinel and for an input
// NUL. A real byte advances Tell() when consumed; the EOF sentinel does not.
// Reject real NUL bytes rather than accepting a prematurely terminated JSON.
// FileReadStream retains its fixed-size fread buffer; no whole-file copy.
template <typename Source>
class CheckedStream {
    Source& source_;
  public:
    using Ch = char;
    explicit CheckedStream(Source& source) : source_(source) {}
    Ch Peek() {
        const Ch value = source_.Peek();
        if (value == '\0') {
            const std::size_t offset = source_.Tell();
            source_.Take();
            if (source_.Tell() != offset)
                throw std::invalid_argument("Unexpected NUL byte at offset " +
                    std::to_string(offset));
        }
        return value;
    }
    Ch Take() {
        const Ch value = Peek();
        if (value != '\0') source_.Take();
        return value;
    }
    std::size_t Tell() const { return source_.Tell(); }
    Ch* PutBegin() { throw std::logic_error("In-situ parsing is disabled"); }
    void Put(Ch) { throw std::logic_error("In-situ parsing is disabled"); }
    void Flush() {}
    std::size_t PutEnd(Ch*) { throw std::logic_error("In-situ parsing is disabled"); }
};

struct FileCloser {
    void operator()(std::FILE* file) const noexcept { std::fclose(file); }
};

struct DiscardStream {
    using Ch = char;
    void Put(char) {}
};

// kParseValidateEncodingFlag validates raw UTF-8, but RapidJSON 1.1 also
// encodes a lone escaped low surrogate. Validate decoded keys/values too.
bool valid_utf8(const char* value, std::size_t length) {
    rapidjson::MemoryStream input(value, length);
    DiscardStream output;
    while (input.Tell() < length) {
        if (!rapidjson::UTF8<>::Validate(input, output)) return false;
    }
    return true;
}

class RJSAXHandler {
    const rj_parser& settings_;
    PathHandler path_;
    std::size_t depth_ = 0;
    std::size_t arguments_ = 0;
    const char* error_ = nullptr;

    bool fail(const char* message) {
        if (error_ == nullptr) error_ = message;
        return false;
    }
    bool check_path() {
        return !path_.m_error || fail("Argument name too long");
    }
    bool begin(bool array) {
        if (depth_ >= settings_.max_depth) return fail("JSON depth limit exceeded");
        if (array) path_.startArraySpecial();
        else path_.push();
        ++depth_;
        return check_path();
    }
    bool end() {
        path_.pop();
        --depth_;
        return check_path();
    }
    bool argument(const char* value, std::size_t length) {
        const std::string name = path_.name();
        if (!check_path()) return false;
        if (length >= JSON_STRING_SIZE) return fail("Argument value too long");
        if (arguments_ >= settings_.max_args)
            return fail("Argument number limit exceeded");
        if (arguments_ == std::numeric_limits<std::size_t>::max())
            return fail("Argument number counter overflow");
        ++arguments_;
        if (settings_.silence) return true;
        const bool ok = std::fwrite(name.data(), 1, name.size(), stdout) == name.size() &&
            std::fwrite(": ", 1, 2, stdout) == 2 &&
            std::fwrite(value, 1, length, stdout) == length &&
            std::fputc('\n', stdout) != EOF;
        return ok || fail("Unable to write JSON argument output");
    }
    bool integer(const std::string& value) { return argument(value.data(), value.size()); }

  public:
    explicit RJSAXHandler(const rj_parser& settings) : settings_(settings) {}
    const char* error() const { return error_; }
    bool StartObject() { return begin(false); }
    bool EndObject(rapidjson::SizeType) { return end(); }
    bool StartArray() { return begin(true); }
    bool EndArray(rapidjson::SizeType) { return end(); }
    bool Key(const char* value, rapidjson::SizeType length, bool) {
        if (length >= JSON_STRING_SIZE) return fail("Argument name too long");
        if (!valid_utf8(value, length)) return fail("Invalid Unicode in JSON key");
        path_.setKey(value, length);
        return check_path();
    }
    bool String(const char* value, rapidjson::SizeType length, bool) {
        if (length >= JSON_STRING_SIZE) return fail("Argument value too long");
        if (!valid_utf8(value, length)) return fail("Invalid Unicode in JSON string");
        return argument(value, length);
    }
    bool RawNumber(const char* value, rapidjson::SizeType length, bool) {
        // Use the original lexeme: no double conversion or integer casts.
        // RapidJSON 1.1 still rejects numeric overflow before this callback.
        return argument(value, length);
    }
    bool Int(int value) { return integer(std::to_string(value)); }
    bool Uint(unsigned int value) { return integer(std::to_string(value)); }
    bool Int64(int64_t value) { return integer(std::to_string(value)); }
    bool Uint64(uint64_t value) { return integer(std::to_string(value)); }
    bool Double(double) { return fail("Unexpected non-lossless JSON number event"); }
    bool Bool(bool value) { return argument(value ? "true" : "false", value ? 4 : 5); }
    bool Null() { return argument("", 0); }
};

template <typename Source>
int parse(rj_parser* parser, Source& source, char** error_msg) {
    RJSAXHandler handler(*parser); // Fresh counters/path, retained settings.
    CheckedStream<Source> input(source);
    rapidjson::GenericReader<rapidjson::UTF8<>, rapidjson::UTF8<>,
        TemporaryAllocator> reader;
    constexpr unsigned flags = rapidjson::kParseNumbersAsStringsFlag |
        rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag;
    const rapidjson::ParseResult result = reader.Parse<flags>(input, handler);
    if (!result) {
        std::string message = handler.error() ? handler.error() :
            rapidjson::GetParseError_En(result.Code());
        message += " at offset " + std::to_string(result.Offset());
        *error_msg = strdup(message.c_str());
        return 2;
    }
    if (!parser->silence && std::fflush(stdout) != 0) {
        *error_msg = strdup("Unable to write JSON argument output");
        return 2;
    }
    return 0;
}

int set_limit(rj_parser* parser, double value, unsigned int* target) {
    if (parser == nullptr || !std::isfinite(value) || value < 0 ||
        value > std::numeric_limits<unsigned int>::max() || std::floor(value) != value)
        return -1;
    *target = static_cast<unsigned int>(value);
    return 0;
}

} // namespace

extern "C" int rj_json_init(rj_parser** parser, char** error_msg) {
    if (error_msg == nullptr) return -1;
    *error_msg = nullptr;
    if (parser == nullptr) return -1;
    *parser = nullptr;
    try {
        *parser = new rj_parser{};
        return 0;
    } catch (const std::exception& e) {
        *error_msg = strdup(e.what());
        return 1;
    } catch (...) {
        *error_msg = strdup("Unable to initialize RapidJSON parser");
        return 1;
    }
}

extern "C" int rj_parse_buffer(rj_parser* parser, const char* buf,
        unsigned int len, char** error_msg) {
    if (error_msg == nullptr) return -1;
    *error_msg = nullptr;
    if (parser == nullptr || buf == nullptr) return -1;
    try {
        rapidjson::MemoryStream input(buf, len);
        return parse(parser, input, error_msg);
    } catch (const std::exception& e) {
        *error_msg = strdup(e.what());
        return 2;
    } catch (...) {
        *error_msg = strdup("Unexpected RapidJSON parser failure");
        return 2;
    }
}

extern "C" int rj_parse_file(rj_parser* parser, const char* filename,
        char** error_msg) {
    if (error_msg == nullptr) return -1;
    *error_msg = nullptr;
    if (parser == nullptr || filename == nullptr) return -1;
    try {
        std::unique_ptr<std::FILE, FileCloser> file(std::fopen(filename, "rb"));
        if (!file) {
            *error_msg = strdup("Unable to open JSON file");
            return 2;
        }
        char buffer[16384];
        rapidjson::FileReadStream input(file.get(), buffer, sizeof(buffer));
        const int result = parse(parser, input, error_msg);
        if (std::ferror(file.get())) {
            std::free(*error_msg);
            *error_msg = strdup("Error reading JSON file");
            return 2;
        }
        return result;
    } catch (const std::exception& e) {
        *error_msg = strdup(e.what());
        return 2;
    } catch (...) {
        *error_msg = strdup("Unexpected RapidJSON parser failure");
        return 2;
    }
}

extern "C" int rj_set_max_depth(rj_parser* parser, double value) {
    return set_limit(parser, value, parser ? &parser->max_depth : nullptr);
}
extern "C" int rj_set_max_arg_num(rj_parser* parser, double value) {
    return set_limit(parser, value, parser ? &parser->max_args : nullptr);
}
extern "C" int rj_set_silence(rj_parser* parser, int silence) {
    if (parser == nullptr) return -1;
    parser->silence = silence != 0;
    return 0;
}
extern "C" int rj_json_cleanup(rj_parser* parser) {
    delete parser;
    return 0;
}

// Preserve the legacy undeclared symbol; destroying the handler manually
// made a later rj_json_cleanup double-destroy its strings and containers.
extern "C" int rj_parser_cleanup(rj_parser* parser) {
    return parser ? 0 : -1;
}

#endif
