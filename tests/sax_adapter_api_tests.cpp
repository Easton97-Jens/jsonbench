// The CLI only accepts positive integral limits. Exercise both SAX adapters'
// public C APIs for zero limits, rejected setters, reuse, and binary-safe I/O.
#include "../src/rjparser.h"
#include "../src/nlparser.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

#if HAVE_RAPIDJSON || HAVE_NLOHMANNJSON
namespace {

class TemporaryInput {
    std::string path_;
  public:
    explicit TemporaryInput(const std::string& text) {
        char name[] = "/tmp/jsonbench-sax-api-XXXXXX";
        const int fd = mkstemp(name);
        if (fd < 0) throw std::runtime_error("mkstemp failed");
        path_ = name;
        std::size_t offset = 0;
        while (offset < text.size()) {
            const ssize_t count = write(fd, text.data() + offset, text.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) {
                close(fd);
                unlink(path_.c_str());
                throw std::runtime_error("temporary input write failed");
            }
            offset += static_cast<std::size_t>(count);
        }
        if (close(fd) != 0) {
            unlink(path_.c_str());
            throw std::runtime_error("temporary input close failed");
        }
    }
    ~TemporaryInput() { unlink(path_.c_str()); }
    TemporaryInput(const TemporaryInput&) = delete;
    TemporaryInput& operator=(const TemporaryInput&) = delete;
    const char* path() const { return path_.c_str(); }
};

class CapturedOutput {
    FILE* file_ = nullptr;
    int saved_fd_ = -1;
  public:
    CapturedOutput() {
        if (std::fflush(stdout) != 0) throw std::runtime_error("stdout flush failed");
        file_ = std::tmpfile();
        if (!file_) throw std::runtime_error("stdout temporary file failed");
        saved_fd_ = dup(STDOUT_FILENO);
        if (saved_fd_ < 0 || dup2(fileno(file_), STDOUT_FILENO) < 0) {
            if (saved_fd_ >= 0) close(saved_fd_);
            std::fclose(file_);
            throw std::runtime_error("stdout capture failed");
        }
    }
    ~CapturedOutput() {
        std::fflush(stdout);
        dup2(saved_fd_, STDOUT_FILENO);
        close(saved_fd_);
        std::fclose(file_);
    }
    CapturedOutput(const CapturedOutput&) = delete;
    CapturedOutput& operator=(const CapturedOutput&) = delete;
    std::string read() {
        if (std::fflush(stdout) != 0 || std::fseek(file_, 0, SEEK_SET) != 0)
            throw std::runtime_error("captured output rewind failed");
        std::string result;
        char buffer[1024];
        std::size_t length;
        while ((length = std::fread(buffer, 1, sizeof buffer, file_)) != 0)
            result.append(buffer, length);
        if (std::ferror(file_)) throw std::runtime_error("captured output read failed");
        return result;
    }
};

// The API replaces the slot and returns free()-owned diagnostics. It must not
// free the caller's previous allocation, including on invalid-argument paths.
class ErrorSlot {
    char* original_;
  public:
    char* value;
    ErrorSlot() : original_(static_cast<char*>(std::malloc(1))), value(original_) {
        if (!original_) throw std::bad_alloc();
        *original_ = '\0';
    }
    ~ErrorSlot() {
        if (value != original_) std::free(value);
        std::free(original_);
    }
    ErrorSlot(const ErrorSlot&) = delete;
    ErrorSlot& operator=(const ErrorSlot&) = delete;
    bool replaced() const { return value != original_; }
};

#if HAVE_RAPIDJSON
struct RapidJsonApi {
    using Handle = rj_parser;
    static const char* name() { return "RapidJSON"; }
    static int init(Handle** p, char** e) { return rj_json_init(p, e); }
    static int buffer(Handle* p, const char* b, unsigned int n, char** e) { return rj_parse_buffer(p, b, n, e); }
    static int file(Handle* p, const char* f, char** e) { return rj_parse_file(p, f, e); }
    static int depth(Handle* p, double n) { return rj_set_max_depth(p, n); }
    static int arguments(Handle* p, double n) { return rj_set_max_arg_num(p, n); }
    static int silence(Handle* p, int n) { return rj_set_silence(p, n); }
    static int cleanup(Handle* p) { return rj_json_cleanup(p); }
};
#endif

#if HAVE_NLOHMANNJSON
struct NlohmannApi {
    using Handle = nl_parser;
    static const char* name() { return "NlohmannJSON"; }
    static int init(Handle** p, char** e) { return nl_json_init(p, e); }
    static int buffer(Handle* p, const char* b, unsigned int n, char** e) { return nl_parse_buffer(p, b, n, e); }
    static int file(Handle* p, const char* f, char** e) { return nl_parse_file(p, f, e); }
    static int depth(Handle* p, double n) { return nl_set_max_depth(p, n); }
    static int arguments(Handle* p, double n) { return nl_set_max_arg_num(p, n); }
    static int silence(Handle* p, int n) { return nl_set_silence(p, n); }
    static int cleanup(Handle* p) { return nl_json_cleanup(p); }
};
#endif

template <typename Api>
class Parser {
  public:
    typename Api::Handle* value = nullptr;
    Parser() {
        ErrorSlot error;
        if (Api::init(&value, &error.value) != 0 || !value || error.value) {
            Api::cleanup(value);
            throw std::runtime_error(std::string(Api::name()) + " initialization failed");
        }
    }
    ~Parser() { Api::cleanup(value); }
    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;
};

struct Result {
    int status;
    bool replaced;
    bool null_error;
    std::string error;
    std::string output;
};

template <typename Api>
Result parse_path(typename Api::Handle* parser, const char* path) {
    ErrorSlot error;
    CapturedOutput output;
    const int status = Api::file(parser, path, &error.value);
    return {status, error.replaced(), !error.value, error.value ? error.value : "", output.read()};
}

template <typename Api>
Result parse(typename Api::Handle* parser, const std::string& text, bool file = false) {
    if (file) {
        TemporaryInput input(text);
        return parse_path<Api>(parser, input.path());
    }
    ErrorSlot error;
    CapturedOutput output;
    const int status = Api::buffer(parser, text.data(), static_cast<unsigned int>(text.size()), &error.value);
    return {status, error.replaced(), !error.value, error.value ? error.value : "", output.read()};
}

class Checks {
    unsigned int total_ = 0;
    unsigned int failures_ = 0;
  public:
    std::string engine;
    void check(const std::string& name, bool passed) {
        ++total_;
        if (!passed) {
            ++failures_;
            std::fprintf(stderr, "FAIL: %s: %s\n", engine.c_str(), name.c_str());
        }
    }
    void success(const std::string& name, const Result& result, const std::string& output = "") {
        check(name + ": succeeds", result.status == 0);
        check(name + ": clears old diagnostic", result.replaced && result.null_error);
        check(name + ": byte-exact output", result.output == output);
        if (result.status != 0)
            std::fprintf(stderr, "  diagnostic: %s\n", result.error.c_str());
    }
    void failure(const std::string& name, const Result& result) {
        check(name + ": fails", result.status != 0);
        check(name + ": fresh malloc-owned diagnostic", result.replaced && !result.null_error && !result.error.empty());
    }
    unsigned int total() const { return total_; }
    unsigned int failures() const { return failures_; }
    int finish() const {
        std::printf("SAX adapter C APIs: %u checks passed, %u failed\n", total_ - failures_, failures_);
        return failures_ ? 1 : 0;
    }
};

template <typename Api>
void invalid_arguments(Checks& checks) {
    {
        ErrorSlot error;
        checks.check("init rejects null parser slot", Api::init(nullptr, &error.value) != 0);
        checks.check("invalid init clears old diagnostic", error.replaced() && !error.value);
    }
    typename Api::Handle* handle = nullptr;
    checks.check("init rejects null diagnostic slot", Api::init(&handle, nullptr) != 0);
    checks.check("invalid init leaves handle null", !handle);
    checks.check("init rejects both null slots", Api::init(nullptr, nullptr) != 0);
    checks.check("cleanup accepts null", Api::cleanup(nullptr) == 0);
    checks.check("depth setter rejects null", Api::depth(nullptr, 1) != 0);
    checks.check("argument setter rejects null", Api::arguments(nullptr, 1) != 0);
    checks.check("silence setter rejects null", Api::silence(nullptr, 1) != 0);
    Parser<Api> parser;
    TemporaryInput input("null");
    for (unsigned int kind = 0; kind < 3; ++kind) {
        ErrorSlot error;
        const int rc = kind == 0 ? Api::buffer(nullptr, "{}", 2, &error.value) :
            Api::buffer(parser.value, nullptr, kind == 1 ? 0 : 1, &error.value);
        checks.check("invalid buffer arguments " + std::to_string(kind), rc != 0);
        checks.check("invalid buffer replaces diagnostic " + std::to_string(kind), error.replaced());
        checks.check("invalid buffer diagnostic is null or nonempty " + std::to_string(kind),
            !error.value || *error.value);
    }
    for (const auto& invalid : std::vector<std::pair<std::string, Result>>{
            {"null parser", parse_path<Api>(nullptr, input.path())},
            {"null filename", parse_path<Api>(parser.value, nullptr)}}) {
        checks.check("file rejects " + invalid.first, invalid.second.status != 0);
        checks.check("file replaces diagnostic for " + invalid.first, invalid.second.replaced);
        checks.check("invalid file emits no output: " + invalid.first, invalid.second.output.empty());
    }
    checks.check("buffer rejects null diagnostic slot", Api::buffer(parser.value, "{}", 2, nullptr) != 0);
    checks.check("file rejects null diagnostic slot", Api::file(parser.value, input.path(), nullptr) != 0);
    checks.failure("missing file", parse_path<Api>(parser.value, (std::string(input.path()) + ".missing").c_str()));
    checks.success("buffer reuse after invalid calls", parse<Api>(parser.value, "null"), ": \n");
    checks.success("file reuse after invalid calls", parse<Api>(parser.value, "true", true), ": true\n");
}

template <typename Api>
void limits(Checks& checks) {
    Parser<Api> parser;
    checks.check("set depth two", Api::depth(parser.value, 2) == 0);
    checks.check("set arguments two", Api::arguments(parser.value, 2) == 0);
    checks.check("enable silence", Api::silence(parser.value, 1) == 0);
    const double upper = static_cast<double>(UINT_MAX);
    const std::vector<std::pair<std::string, double>> invalid = {
        {"NaN", std::numeric_limits<double>::quiet_NaN()},
        {"infinity", std::numeric_limits<double>::infinity()},
        {"negative infinity", -std::numeric_limits<double>::infinity()},
        {"negative integer", -1.0}, {"negative fraction", -0.5},
        {"fraction below one", 0.5}, {"positive fraction", 1.5},
        {"above UINT_MAX", upper + 1.0}
    };
    for (const auto& limit : invalid) {
        checks.check("depth rejects " + limit.first, Api::depth(parser.value, limit.second) != 0);
        checks.check("argument count rejects " + limit.first, Api::arguments(parser.value, limit.second) != 0);
        checks.success("rejected " + limit.first + " preserves limits", parse<Api>(parser.value, "{\"a\":[null,true]}"));
        checks.failure("rejected " + limit.first + " preserves depth ceiling", parse<Api>(parser.value, "{\"a\":[[null]]}"));
        checks.failure("rejected " + limit.first + " preserves argument ceiling", parse<Api>(parser.value, "[null,true,false]"));
    }
    checks.check("depth accepts UINT_MAX", Api::depth(parser.value, upper) == 0);
    checks.check("argument count accepts UINT_MAX", Api::arguments(parser.value, upper) == 0);
    checks.success("maximum limits permit ordinary JSON", parse<Api>(parser.value, "{\"a\":[null,true,false]}"));
    checks.check("depth accepts zero", Api::depth(parser.value, 0) == 0);
    for (const auto& value : {"null", "true", "false", "1", "\"value\""})
        checks.success(std::string("zero depth allows root ") + value, parse<Api>(parser.value, value));
    for (const auto& value : {"{}", "[]"})
        checks.failure(std::string("zero depth rejects ") + value, parse<Api>(parser.value, value));
    checks.check("restore depth three", Api::depth(parser.value, 3) == 0);
    checks.check("argument count accepts zero", Api::arguments(parser.value, 0) == 0);
    for (const auto& value : {"{}", "[]", "{\"a\":[{},[]]}"})
        checks.success(std::string("zero arguments allows ") + value, parse<Api>(parser.value, value));
    for (const auto& value : {"null", "true", "false", "0", "\"\"", "{\"a\":null}", "[false]"})
        checks.failure(std::string("zero arguments rejects ") + value, parse<Api>(parser.value, value));
    checks.check("restore arguments two", Api::arguments(parser.value, 2) == 0);
    checks.success("exact depth", parse<Api>(parser.value, "[[[null]]]"));
    checks.failure("above depth", parse<Api>(parser.value, "[[[[null]]]]"));
    checks.success("bool and null exactly at count", parse<Api>(parser.value, "{\"a\":null,\"b\":false}"));
    checks.failure("bool and null above count", parse<Api>(parser.value, "[null,true,false]"));
    checks.check("negative zero is a valid zero depth", Api::depth(parser.value, -0.0) == 0);
    checks.failure("negative zero has zero-limit semantics", parse<Api>(parser.value, "{}"));
}

template <typename Api>
void reuse_and_output(Checks& checks) {
    Parser<Api> parser;
    checks.check("set reuse depth", Api::depth(parser.value, 2) == 0);
    checks.check("set reuse arguments", Api::arguments(parser.value, 2) == 0);
    for (bool file : {false, true}) {
        const std::string kind = file ? "file" : "buffer";
        const Result truncated = parse<Api>(parser.value, "{\"leaked\":{\"x\":true,\"y\":", file);
        checks.failure(kind + " truncated input", truncated);
        checks.check(kind + " emits completed prefix only", truncated.output == "leaked.x: true\n");
        checks.success(kind + " fresh path after failure", parse<Api>(parser.value, "{\"fresh\":false}", file), "fresh: false\n");
        checks.success(kind + " root array", parse<Api>(parser.value, "[null,true]", file), "array.array: \narray.array: true\n");
        checks.success(kind + " root scalar resets array path", parse<Api>(parser.value, "2", file), ": 2\n");
        const Result count = parse<Api>(parser.value, "[null,true,false]", file);
        checks.failure(kind + " retained argument limit", count);
        checks.check(kind + " only permitted arguments printed", count.output == "array.array: \narray.array: true\n");
        checks.success(kind + " resets count after failure", parse<Api>(parser.value, "{\"a\":true,\"b\":null}", file), "a: true\nb: \n");
        checks.failure(kind + " retained depth limit", parse<Api>(parser.value, "[[[null]]]", file));
        checks.success(kind + " resets after depth failure", parse<Api>(parser.value, "{\"after\":null}", file), "after: \n");
        const char binary[] = "a\0b.c\0d: v\0x\na\0b.after: false\n";
        checks.success(kind + " embedded NUL values and paths", parse<Api>(parser.value,
            "{\"a\\u0000b\":{\"c\\u0000d\":\"v\\u0000x\",\"after\":false}}", file),
            std::string(binary, sizeof binary - 1));
        checks.success(kind + " full dotted parent restoration", parse<Api>(parser.value,
            "{\"a.b\":{\"c.d\":1},\"after\":2}", file), "a.b.c.d: 1\nafter: 2\n");
        checks.success(kind + " EOF root number is finalized", parse<Api>(parser.value, "1.250e+3", file), ": 1.250e+3\n");
        checks.failure(kind + " empty input", parse<Api>(parser.value, "", file));
        checks.failure(kind + " truncated EOF number", parse<Api>(parser.value, "1e+", file));
    }
    checks.check("nonzero silence accepted", Api::silence(parser.value, -7) == 0);
    checks.success("silent parsing validates and suppresses output", parse<Api>(parser.value, "{\"a\":null,\"b\":true}"));
    checks.failure("silence retains argument limit", parse<Api>(parser.value, "[null,true,false]"));
    checks.failure("silence retains syntax validation", parse<Api>(parser.value, "{\"a\":true,}"));
    checks.success("silence persists across failed file reuse", parse<Api>(parser.value, "{\"quiet\":false}", true));
}

template <typename Api>
void stream_boundaries(Checks& checks) {
    Parser<Api> parser;
    checks.check("silence boundary failures", Api::silence(parser.value, 1) == 0);
    for (const std::size_t offset : {16383u, 16384u, 16385u}) {
        const std::string raw_nul = "{}" + std::string(offset - 2, ' ') + std::string(1, '\0') + "{}";
        for (bool file : {false, true}) {
            const std::string label = (file ? "file" : "buffer") +
                std::string(" raw NUL at offset ") + std::to_string(offset);
            const Result result = parse<Api>(parser.value, raw_nul, file);
            checks.failure(label, result);
            checks.check(label + ": no silent output", result.output.empty());
        }
    }
    const std::string trailing = "{}" + std::string(16382, ' ') + "[]";
    checks.failure("trailing material after file buffer boundary", parse<Api>(parser.value, trailing, true));
    checks.failure("trailing material in buffer API", parse<Api>(parser.value, trailing));
    checks.check("disable silence for valid boundary output", Api::silence(parser.value, 0) == 0);
    const std::string unicode = std::string(16382, ' ') + "\"\xc3\xa4\xf0\x9f\x98\x80\"";
    checks.success("UTF-8 spans file buffer boundary", parse<Api>(parser.value, unicode, true), ": \xc3\xa4\xf0\x9f\x98\x80\n");
    const std::string escape = std::string(16382, ' ') + "\"\\u0000x\"";
    const char binary[] = ": \0x\n";
    checks.success("decoded NUL escape spans file buffer boundary", parse<Api>(parser.value, escape, true),
        std::string(binary, sizeof binary - 1));
    checks.success("reused adapter after boundary failures", parse<Api>(parser.value, "{\"fresh\":true}"), "fresh: true\n");
}

template <typename Api>
void run(Checks& checks) {
    checks.engine = Api::name();
    const unsigned int initial_total = checks.total();
    const unsigned int initial_failures = checks.failures();
    invalid_arguments<Api>(checks);
    limits<Api>(checks);
    reuse_and_output<Api>(checks);
    stream_boundaries<Api>(checks);
    const unsigned int total = checks.total() - initial_total;
    const unsigned int failures = checks.failures() - initial_failures;
    std::printf("%s C API: %u checks passed, %u failed\n", Api::name(), total - failures, failures);
}
} // namespace

int main() {
    try {
        Checks checks;
#if HAVE_RAPIDJSON
        run<RapidJsonApi>(checks);
#endif
#if HAVE_NLOHMANNJSON
        run<NlohmannApi>(checks);
#endif
        return checks.finish();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: SAX adapter C API test setup: %s\n", error.what());
        return 1;
    }
}
#else
int main() {
    std::fprintf(stderr, "SAX adapter C API tests require RapidJSON or NlohmannJSON\n");
    return 77;
}
#endif
