#include "rjparser.h"

#if HAVE_RAPIDJSON

#include "pathhandler.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

class RapidJsonInputError final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

static_assert(alignof(rapidjson::SizeType) <= alignof(std::max_align_t),
    "RapidJSON state must fit fundamental allocation alignment");

constexpr std::size_t scratch_budget =
    static_cast<std::size_t>(JSON_STRING_SIZE) * 4 + 16384;

// Bound the vector's actual allocation, including any implementation-specific
// reserve rounding. GenericReader also stores SizeType scalars in this storage.
template <typename T>
class ScratchAllocator {
  public:
    using value_type = T;
    ScratchAllocator() = default;
    template <typename U> explicit ScratchAllocator(const ScratchAllocator<U>&) {}
    T* allocate(std::size_t count) const {
        if (count > scratch_budget / sizeof(T))
            throw RapidJsonInputError("RapidJSON token/state allocation limit exceeded");
        std::allocator<T> allocator;
        auto* memory = allocator.allocate(count);
        // The installed standard allocator uses global operator new, which
        // meets this alignment. Check it explicitly for other implementations.
        if (reinterpret_cast<std::uintptr_t>(memory) % alignof(rapidjson::SizeType) != 0) {
            allocator.deallocate(memory, count);
            throw RapidJsonInputError("RapidJSON scratch storage is not aligned");
        }
        return memory;
    }
    void deallocate(T* data, std::size_t count) const noexcept {
        std::allocator<T>{}.deallocate(data, count);
    }
    template <typename U> bool operator==(const ScratchAllocator<U>&) const noexcept { return true; }
    template <typename U> bool operator!=(const ScratchAllocator<U>&) const noexcept { return false; }
};

// Each parse here creates a fresh GenericReader with its own stack allocator.
// Keep its UTF-8 token and scalar state storage in one bounded vector. Like
// RapidJSON's public MemoryPoolAllocator, Free is a no-op: ownership remains
// with the allocator, whose vector releases all storage at destruction.
class TemporaryAllocator {
    std::vector<char, ScratchAllocator<char>> storage_;
  public:
    static constexpr bool kNeedFree = false;
    char* Malloc(std::size_t size) { return Realloc(nullptr, 0, size); }
    char* Realloc(char* data, std::size_t old_size, std::size_t size) {
        if (size > scratch_budget)
            throw RapidJsonInputError("RapidJSON token/state allocation limit exceeded");
        if ((data == nullptr && old_size != 0) ||
            (data != nullptr && (data != storage_.data() || old_size != storage_.size())) ||
            (data == nullptr && !storage_.empty()))
            throw RapidJsonInputError("Invalid RapidJSON scratch allocation");
        if (size == 0) {
            storage_.clear();
            return nullptr;
        }
        // reserve has the strong exception guarantee for char; a failed growth
        // leaves the reader's old pointer and bytes valid. Reserve first so
        // resize cannot request its usual geometric growth beyond the budget.
        storage_.reserve(size);
        storage_.resize(size);
        return storage_.data();
    }
    static void Free(char*) noexcept {
        // The owning vector is released when GenericReader deletes its allocator.
    }
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
                throw RapidJsonInputError("Unexpected NUL byte at offset " +
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
    Ch* PutBegin() const { throw RapidJsonInputError("In-situ parsing is disabled"); }
    void Put(Ch) const { throw RapidJsonInputError("In-situ parsing is disabled"); }
    void Flush() const {
        // Required by the RapidJSON stream concept; input has no output buffer.
    }
    std::size_t PutEnd(Ch*) const { throw RapidJsonInputError("In-situ parsing is disabled"); }
};

struct FileCloser {
    void operator()(std::FILE* file) const noexcept { std::fclose(file); }
};

struct DiscardStream {
    using Ch = char;
    void Put(char) const {
        // Validate decoded UTF-8 without storing a second copy of the token.
    }
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
    bool integer(std::string_view value) { return argument(value.data(), value.size()); }

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
int parse(const rj_parser* parser, Source& source, char** error_msg,
        std::FILE* file = nullptr) {
    RJSAXHandler handler(*parser); // Fresh counters/path, retained settings.
    CheckedStream<Source> input(source);
    rapidjson::GenericReader<rapidjson::UTF8<>, rapidjson::UTF8<>,
        TemporaryAllocator> reader;
    constexpr unsigned flags = rapidjson::kParseNumbersAsStringsFlag |
        rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag;
    const rapidjson::ParseResult result = reader.Parse<flags>(input, handler);
    if (file != nullptr && std::ferror(file)) {
        *error_msg = strdup("Error reading JSON file");
        return 2;
    }
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

int set_limit(const rj_parser* parser, double value, unsigned int* target) {
    if (parser == nullptr || !std::isfinite(value) || value < 0 ||
        value > std::numeric_limits<unsigned int>::max() || std::floor(value) != value)
        return -1;
    *target = static_cast<unsigned int>(value);
    return 0;
}

// Contain exceptions at the C interface after handling the concrete failures
// produced by input validation, allocation, and standard string operations.
template <typename Operation>
int at_c_boundary(Operation&& operation, char** error_msg, int failure_status = 2) noexcept {
    try {
        return operation();
    } catch (const RapidJsonInputError& error) {
        *error_msg = strdup(error.what());
    } catch (const std::bad_alloc& error) {
        *error_msg = strdup(error.what());
    } catch (const std::length_error& error) {
        *error_msg = strdup(error.what());
    } catch (...) {
        // Third-party exceptions of unknown type must never cross the C ABI.
        *error_msg = strdup("Unexpected RapidJSON parser failure");
    }
    return failure_status;
}

} // namespace

extern "C" int rj_json_init(rj_parser** parser, char** error_msg) {
    if (error_msg == nullptr) return -1;
    *error_msg = nullptr;
    if (parser == nullptr) return -1;
    *parser = nullptr;
    return at_c_boundary([parser] {
        *parser = std::make_unique<rj_parser>().release();
        return 0;
    }, error_msg, 1);
}

extern "C" int rj_parse_buffer(rj_parser* parser, const char* buf,
        unsigned int len, char** error_msg) {
    if (error_msg == nullptr) return -1;
    *error_msg = nullptr;
    if (parser == nullptr || buf == nullptr) return -1;
    return at_c_boundary([parser, buf, len, error_msg] {
        rapidjson::MemoryStream input(buf, len);
        return parse(parser, input, error_msg);
    }, error_msg);
}

extern "C" int rj_parse_file(rj_parser* parser, const char* filename,
        char** error_msg) {
    if (error_msg == nullptr) return -1;
    *error_msg = nullptr;
    if (parser == nullptr || filename == nullptr) return -1;
    return at_c_boundary([parser, filename, error_msg] {
        const std::unique_ptr<std::FILE, FileCloser> file(std::fopen(filename, "rb"));
        if (!file) {
            *error_msg = strdup("Unable to open JSON file");
            return 2;
        }
        std::array<char, 16384> buffer{};
        rapidjson::FileReadStream input(file.get(), buffer.data(), buffer.size());
        return parse(parser, input, error_msg, file.get());
    }, error_msg);
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
    const std::unique_ptr<rj_parser> owned(parser);
    return 0;
}

// Preserve the legacy undeclared symbol; destroying the handler manually
// made a later rj_json_cleanup double-destroy its strings and containers.
extern "C" int rj_parser_cleanup(rj_parser* parser) {
    return parser ? 0 : -1;
}

#endif
