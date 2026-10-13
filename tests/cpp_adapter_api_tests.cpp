// File-based C API contracts for the native JsonCpp DOM and simdjson On-Demand
// adapters. These complement the common CLI matrix with zero limits, reuse,
// numeric capability differences, and bounded traversal with enlarged limits.
#include "../src/jsoncppparser.h"
#include "../src/simdjsonparser.h"

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

#if HAVE_JSONCPP || HAVE_SIMDJSON
namespace {

class Input {
    std::string path_;
  public:
    explicit Input(const std::string& text) {
        char name[] = "/tmp/jsonbench-cpp-api-XXXXXX";
        const int fd = mkstemp(name);
        if (fd < 0) throw std::runtime_error("mkstemp failed");
        path_ = name;
        std::size_t offset = 0;
        while (offset < text.size()) {
            const ssize_t count = write(fd, text.data() + offset, text.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) {
                close(fd); unlink(path_.c_str());
                throw std::runtime_error("temporary input write failed");
            }
            offset += static_cast<std::size_t>(count);
        }
        if (close(fd) != 0) {
            unlink(path_.c_str());
            throw std::runtime_error("temporary input close failed");
        }
    }
    ~Input() { unlink(path_.c_str()); }
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    const char* path() const { return path_.c_str(); }
};

class Capture {
    FILE* file_ = nullptr;
    int saved_ = -1;
  public:
    Capture() {
        if (std::fflush(stdout) != 0) throw std::runtime_error("stdout flush failed");
        file_ = std::tmpfile();
        if (!file_) throw std::runtime_error("output temporary file failed");
        saved_ = dup(STDOUT_FILENO);
        if (saved_ < 0 || dup2(fileno(file_), STDOUT_FILENO) < 0) {
            if (saved_ >= 0) close(saved_);
            std::fclose(file_);
            throw std::runtime_error("stdout capture failed");
        }
    }
    ~Capture() {
        std::fflush(stdout);
        dup2(saved_, STDOUT_FILENO);
        close(saved_);
        std::fclose(file_);
    }
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;
    std::string read() {
        if (std::fflush(stdout) != 0 || std::fseek(file_, 0, SEEK_SET) != 0)
            throw std::runtime_error("output rewind failed");
        std::string output;
        char buffer[1024];
        std::size_t count;
        while ((count = std::fread(buffer, 1, sizeof buffer, file_)) != 0)
            output.append(buffer, count);
        if (std::ferror(file_)) throw std::runtime_error("output read failed");
        return output;
    }
};

// The incoming diagnostic belongs to the caller. Successful parsing resets
// the slot without freeing it; failed parsing supplies a free()-owned message.
class ErrorSlot {
    char* old_;
  public:
    char* value;
    ErrorSlot() : old_(static_cast<char*>(std::malloc(1))), value(old_) {
        if (!old_) throw std::bad_alloc();
        *old_ = '\0';
    }
    ~ErrorSlot() {
        if (value != old_) std::free(value);
        std::free(old_);
    }
    ErrorSlot(const ErrorSlot&) = delete;
    ErrorSlot& operator=(const ErrorSlot&) = delete;
    bool replaced() const { return value != old_; }
};

#define FILE_API(Type, Prefix, Label, DuplicateKeys, WideNumbers, SafeDepth) \
struct Type { \
    using Handle = Prefix##_parser; \
    static const char* name() { return Label; } \
    static constexpr bool duplicates = DuplicateKeys; \
    static constexpr bool wide_numbers = WideNumbers; \
    static constexpr unsigned int safe_depth = SafeDepth; \
    static int init(Handle** p, char** e) { return Prefix##_json_init(p, e); } \
    static int file(Handle* p, const char* f, char** e) { return Prefix##_parse_file(p, f, e); } \
    static int depth(Handle* p, double n) { return Prefix##_set_max_depth(p, n); } \
    static int arguments(Handle* p, double n) { return Prefix##_set_max_arg_num(p, n); } \
    static int silence(Handle* p, int n) { return Prefix##_set_silence(p, n); } \
    static int cleanup(Handle* p) { return Prefix##_json_cleanup(p); } \
};

#if HAVE_JSONCPP
FILE_API(JsonCppApi, jsoncpp, "JsonCpp", false, true, 999)
#endif
#if HAVE_SIMDJSON
FILE_API(SimdJsonApi, simdjson, "simdjson", true, false, 512)
#endif
#undef FILE_API

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
    Capture capture;
    const int status = Api::file(parser, path, &error.value);
    return {status, error.replaced(), !error.value,
        error.value ? error.value : "", capture.read()};
}

template <typename Api>
Result parse(typename Api::Handle* parser, const std::string& text) {
    Input input(text);
    return parse_path<Api>(parser, input.path());
}

class Checks {
    unsigned int passed_ = 0;
    unsigned int failed_ = 0;
  public:
    std::string engine;
    void check(const std::string& name, bool ok) {
        if (ok) ++passed_;
        else {
            ++failed_;
            std::fprintf(stderr, "FAIL: %s: %s\n", engine.c_str(), name.c_str());
        }
    }
    void success(const std::string& name, const Result& result, const std::string& expected = "") {
        check(name + ": success and reset diagnostic",
            result.status == 0 && result.replaced && result.null_error);
        check(name + ": exact output", result.output == expected);
        if (result.status != 0) std::fprintf(stderr, "  diagnostic: %s\n", result.error.c_str());
    }
    void failure(const std::string& name, const Result& result, const std::string& diagnostic = "") {
        check(name + ": failure and owned diagnostic", result.status != 0 &&
            result.replaced && !result.null_error && !result.error.empty());
        if (!diagnostic.empty())
            check(name + ": diagnostic identifies limit", result.error.find(diagnostic) != std::string::npos);
    }
    int finish() const {
        std::printf("C++ adapter C APIs: %u checks passed, %u failed\n", passed_, failed_);
        return failed_ ? 1 : 0;
    }
};

template <typename Api>
void invalid_arguments(Checks& checks) {
    typename Api::Handle* handle = nullptr;
    ErrorSlot error;
    checks.check("null initialization slots", Api::init(nullptr, &error.value) != 0 &&
        Api::init(&handle, nullptr) != 0 && !handle && Api::init(nullptr, nullptr) != 0);
    checks.check("null setters and cleanup", Api::depth(nullptr, 1) != 0 &&
        Api::arguments(nullptr, 1) != 0 && Api::silence(nullptr, 1) != 0 && Api::cleanup(nullptr) == 0);
    Parser<Api> parser;
    Input input("true");
    const Result null_parser = parse_path<Api>(nullptr, input.path());
    const Result null_filename = parse_path<Api>(parser.value, nullptr);
    checks.check("invalid parser/filename reset diagnostics and emit nothing",
        null_parser.status != 0 && null_parser.replaced && null_parser.null_error && null_parser.output.empty() &&
        null_filename.status != 0 && null_filename.replaced && null_filename.null_error && null_filename.output.empty());
    checks.check("null diagnostic slot rejected", Api::file(parser.value, input.path(), nullptr) != 0);
    checks.failure("missing file", parse_path<Api>(parser.value, (std::string(input.path()) + ".missing").c_str()));
    checks.success("reuse after invalid API calls", parse<Api>(parser.value, "true"), ": true\n");
}

template <typename Api>
void limits(Checks& checks) {
    Parser<Api> parser;
    checks.check("configure two containers and arguments", Api::depth(parser.value, 2) == 0 &&
        Api::arguments(parser.value, 2) == 0 && Api::silence(parser.value, 1) == 0);
    const std::vector<double> invalid = {-1, -0.5, 0.5, 1.5,
        std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(), static_cast<double>(UINT_MAX) + 1};
    for (std::size_t i = 0; i < invalid.size(); ++i) {
        const std::string label = "invalid setter " + std::to_string(i);
        checks.check(label, Api::depth(parser.value, invalid[i]) != 0 && Api::arguments(parser.value, invalid[i]) != 0);
        checks.success(label + " preserves exact limits", parse<Api>(parser.value, "{\"a\":[true,null]}"));
        checks.failure(label + " preserves depth ceiling", parse<Api>(parser.value, "[[[]]]"));
        checks.failure(label + " preserves count ceiling", parse<Api>(parser.value, "[true,null,false]"));
    }
    checks.check("zero depth accepted", Api::depth(parser.value, -0.0) == 0);
    for (const char* value : {"true", "null", "42", "\"value\""})
        checks.success(std::string("root scalar at depth zero ") + value, parse<Api>(parser.value, value));
    for (const char* value : {"{}", "[]"})
        checks.failure(std::string("container at depth zero ") + value, parse<Api>(parser.value, value), "depth");
    checks.check("zero count accepted", Api::depth(parser.value, 3) == 0 && Api::arguments(parser.value, 0) == 0);
    checks.success("empty containers at count zero", parse<Api>(parser.value, "{\"a\":[{},[]]}"));
    for (const char* value : {"true", "false", "null", "0", "\"\""})
        checks.failure(std::string("scalar at count zero ") + value, parse<Api>(parser.value, value), "Argument number");
    checks.check("restore exact argument limit", Api::arguments(parser.value, 3) == 0);
    checks.success("all scalar kinds counted exactly", parse<Api>(parser.value, "[[[null,true,false]]]"));
    checks.failure("four scalar leaves exceed count", parse<Api>(parser.value, "[null,true,false,0]"));
}

template <typename Api>
void reuse_and_numbers(Checks& checks) {
    Parser<Api> parser;
    checks.success("source order and structural path restoration", parse<Api>(parser.value,
        "{\"z\":[{\"b\":true},null],\"a.b\":{\"c\":2},\"after\":false}"),
        "z.z.b: true\nz.z: \na.b.c: 2\nafter: false\n");
    const char binary[] = "a\0b: x\0y\n";
    checks.success("embedded NUL name and value", parse<Api>(parser.value, "{\"a\\u0000b\":\"x\\u0000y\"}"),
        std::string(binary, sizeof binary - 1));
    for (const char* invalid : {"[true,]", "1e+", "\"\\udc00\""}) {
        checks.failure("syntax rejection", parse<Api>(parser.value, invalid));
        checks.success("clean state after syntax failure", parse<Api>(parser.value, "{\"fresh\":false}"), "fresh: false\n");
    }
    const std::string finite = "{\"min\":-9223372036854775808,\"max\":9223372036854775807,"
        "\"u\":18446744073709551615,\"d\":0.123456789012345678901234567890,\"e\":1.2300e+03,\"n\":-0}";
    checks.success("lossless numeric spelling", parse<Api>(parser.value, finite),
        "min: -9223372036854775808\nmax: 9223372036854775807\nu: 18446744073709551615\n"
        "d: 0.123456789012345678901234567890\ne: 1.2300e+03\nn: -0\n");
    for (const char* number : {"18446744073709551616", "-9223372036854775809", "1e9999"}) {
        if (Api::wide_numbers)
            checks.success("raw numeric range retained", parse<Api>(parser.value, number), std::string(": ") + number + '\n');
        else checks.failure("unsupported native numeric range rejected", parse<Api>(parser.value, number));
    }
    const Result duplicate = parse<Api>(parser.value, "{\"k\":1,\"k\":2}");
    if (Api::duplicates) checks.success("duplicate keys preserved", duplicate, "k: 1\nk: 2\n");
    else checks.failure("duplicate keys explicitly rejected", duplicate, "Duplicate key");
    checks.check("nonzero silence accepted", Api::silence(parser.value, -7) == 0);
    checks.success("silence suppresses output", parse<Api>(parser.value, "{\"quiet\":true}"));
    checks.failure("silence retains syntax validation", parse<Api>(parser.value, "{} []"));
    checks.failure("silence rejects trailing raw NUL", parse<Api>(parser.value, std::string("{}\0", 3)));
    checks.failure("oversized token rejected", parse<Api>(parser.value, "\"" + std::string(JSON_STRING_SIZE, 'x') + "\""));
    checks.success("reuse after native and resource failures", parse<Api>(parser.value, "[true,null]"));
}

template <typename Api>
void large_depths(Checks& checks) {
    Parser<Api> parser;
    checks.check("enlarged integral limits accepted", Api::depth(parser.value, UINT_MAX) == 0 &&
        Api::arguments(parser.value, UINT_MAX) == 0);
    for (bool silent : {false, true}) {
        checks.check("set deep traversal mode", Api::silence(parser.value, silent) == 0);
        for (unsigned int depth : {500u, 501u, 512u, 513u, 999u, 2000u}) {
            for (bool leaf : {false, true}) {
                const std::string text = std::string(depth, '[') + (leaf ? "true" : "") + std::string(depth, ']');
                const std::string label = "depth " + std::to_string(depth) +
                    (leaf ? " boolean leaf" : " empty arrays") + (silent ? " silent" : " output");
                if (depth <= Api::safe_depth) {
                    std::string expected;
                    if (leaf && !silent) {
                        expected = "array";
                        for (unsigned int i = 0; i < depth; ++i) expected += ".array";
                        expected += ": true\n";
                    }
                    checks.success(label, parse<Api>(parser.value, text), expected);
                } else checks.failure(label, parse<Api>(parser.value, text), Api::duplicates ? "depth" : "stackLimit");
            }
        }
    }
    checks.check("restore default depth and one argument", Api::depth(parser.value, 500) == 0 &&
        Api::arguments(parser.value, 1) == 0 && Api::silence(parser.value, 1) == 0);
    checks.success("default depth exact", parse<Api>(parser.value, std::string(500, '[') + "true" + std::string(500, ']')));
    checks.failure("default depth exceeded", parse<Api>(parser.value, std::string(501, '[') + "true" + std::string(501, ']')));
    checks.failure("boolean leaf consumes count", parse<Api>(parser.value, "[true,false]"));
    checks.success("parser reusable after deep failures", parse<Api>(parser.value, "true"));
}

template <typename Api>
void run(Checks& checks) {
    checks.engine = Api::name();
    invalid_arguments<Api>(checks);
    limits<Api>(checks);
    reuse_and_numbers<Api>(checks);
    large_depths<Api>(checks);
}

} // namespace
#endif

int main() {
#if !HAVE_JSONCPP && !HAVE_SIMDJSON
    std::puts("SKIP C++ adapter C APIs: neither JsonCpp nor simdjson enabled");
    return 77;
#else
    try {
        Checks checks;
#if HAVE_JSONCPP
        run<JsonCppApi>(checks);
#endif
#if HAVE_SIMDJSON
        run<SimdJsonApi>(checks);
#endif
        return checks.finish();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "C++ adapter C API test harness failed: %s\n", error.what());
        return 1;
    }
#endif
}
