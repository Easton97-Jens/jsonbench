// Direct regression checks for the C interface, including parser reuse and
// limits that cannot be represented by the CLI's positive-integer options.
#include "../src/jsonconsparser.h"

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

#if HAVE_JSONCONS
namespace {

class TemporaryInput {
    std::string path_;

  public:
    explicit TemporaryInput(const std::string& text) {
        char name[] = "/tmp/jsonbench-jsoncons-api-XXXXXX";
        const int fd = mkstemp(name);
        if (fd < 0) throw std::runtime_error("mkstemp failed");
        path_ = name;
        std::size_t written = 0;
        while (written < text.size()) {
            const ssize_t count = write(fd, text.data() + written, text.size() - written);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) {
                close(fd);
                unlink(path_.c_str());
                throw std::runtime_error("temporary input write failed");
            }
            written += static_cast<std::size_t>(count);
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
        if (file_ == nullptr) throw std::runtime_error("stdout temporary file failed");
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
        std::string text;
        char buffer[1024];
        std::size_t count;
        while ((count = std::fread(buffer, 1, sizeof buffer, file_)) != 0)
            text.append(buffer, count);
        if (std::ferror(file_)) throw std::runtime_error("captured output read failed");
        return text;
    }
};

// Start each error slot with a caller-owned allocation. The API must reset
// the pointer, not free the caller's old value. Free returned diagnostics with
// free(), as required by the C interface, on success and failure paths alike.
class ErrorSlot {
    char* original_;

  public:
    char* value;
    ErrorSlot() : original_(static_cast<char*>(std::malloc(1))), value(original_) {
        if (original_ == nullptr) throw std::bad_alloc();
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

class Parser {
  public:
    jsoncons_parser* value = nullptr;
    Parser() {
        ErrorSlot error;
        const int rc = jsoncons_json_init(&value, &error.value);
        if (rc != 0 || value == nullptr || error.value != nullptr) {
            jsoncons_json_cleanup(value);
            throw std::runtime_error("parser initialization failed");
        }
    }
    ~Parser() { jsoncons_json_cleanup(value); }
    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;
};

struct ParseResult {
    int status;
    bool error_replaced;
    bool error_is_null;
    std::string error;
    std::string output;
};

ParseResult parse_path(jsoncons_parser* parser, const char* path) {
    ErrorSlot error;
    CapturedOutput output;
    const int status = jsoncons_parse_file(parser, path, &error.value);
    return {status, error.replaced(), error.value == nullptr,
        error.value != nullptr ? error.value : "", output.read()};
}

ParseResult parse(jsoncons_parser* parser, const std::string& text) {
    TemporaryInput input(text);
    return parse_path(parser, input.path());
}

class Checks {
    unsigned int total_ = 0;
    unsigned int failures_ = 0;

  public:
    void check(const std::string& name, bool passed) {
        ++total_;
        if (!passed) {
            ++failures_;
            std::fprintf(stderr, "FAIL: %s\n", name.c_str());
        }
    }
    void success(const std::string& name, const ParseResult& result,
            const std::string& expected = "") {
        check(name + ": succeeds", result.status == 0);
        check(name + ": clears previous error", result.error_replaced && result.error_is_null);
        check(name + ": exact output", result.output == expected);
        if (result.status != 0)
            std::fprintf(stderr, "  diagnostic: %s\n", result.error.c_str());
    }
    void failure(const std::string& name, const ParseResult& result) {
        check(name + ": fails", result.status != 0);
        check(name + ": returns fresh malloc-owned diagnostic",
            result.error_replaced && !result.error_is_null && !result.error.empty());
    }
    int finish() const {
        std::printf("jsoncons C API: %u checks passed, %u failed\n",
            total_ - failures_, failures_);
        return failures_ == 0 ? 0 : 1;
    }
};

void invalid_arguments(Checks& checks) {
    {
        ErrorSlot error;
        checks.check("init rejects null parser slot", jsoncons_json_init(nullptr, &error.value) != 0);
        checks.check("failed init clears previous error", error.replaced() && error.value == nullptr);
    }
    jsoncons_parser* null_parser = nullptr;
    checks.check("init rejects null error slot", jsoncons_json_init(&null_parser, nullptr) != 0);
    checks.check("failed init leaves null parser", null_parser == nullptr);
    checks.check("init rejects both null slots", jsoncons_json_init(nullptr, nullptr) != 0);
    checks.check("cleanup accepts null", jsoncons_json_cleanup(nullptr) == 0);
    checks.check("depth setter rejects null parser", jsoncons_set_max_depth(nullptr, 1) != 0);
    checks.check("argument setter rejects null parser", jsoncons_set_max_arg_num(nullptr, 1) != 0);
    checks.check("silence setter rejects null parser", jsoncons_set_silence(nullptr, 1) != 0);

    Parser parser;
    TemporaryInput input("null");
    const ParseResult no_parser = parse_path(nullptr, input.path());
    checks.check("parse rejects null parser", no_parser.status != 0);
    checks.check("invalid parser clears previous error", no_parser.error_replaced && no_parser.error_is_null);
    const ParseResult no_filename = parse_path(parser.value, nullptr);
    checks.check("parse rejects null filename", no_filename.status != 0);
    checks.check("invalid filename clears previous error", no_filename.error_replaced && no_filename.error_is_null);
    checks.check("parse rejects null error slot",
        jsoncons_parse_file(parser.value, input.path(), nullptr) != 0);
    checks.failure("missing file", parse_path(parser.value, (std::string(input.path()) + ".missing").c_str()));
    checks.success("parse remains usable after invalid calls", parse(parser.value, "null"), ": \n");
}

void limit_setters(Checks& checks) {
    Parser parser;
    checks.check("set depth to two", jsoncons_set_max_depth(parser.value, 2) == 0);
    checks.check("set arguments to two", jsoncons_set_max_arg_num(parser.value, 2) == 0);
    checks.check("enable silence", jsoncons_set_silence(parser.value, 1) == 0);
    const double upper = static_cast<double>(UINT_MAX);
    const std::vector<std::pair<std::string, double>> invalid = {
        {"NaN", std::numeric_limits<double>::quiet_NaN()},
        {"positive infinity", std::numeric_limits<double>::infinity()},
        {"negative infinity", -std::numeric_limits<double>::infinity()},
        {"negative integer", -1.0}, {"negative fraction", -0.5},
        {"fraction below one", 0.5}, {"positive fraction", 1.5},
        {"above UINT_MAX", upper + 1.0}, {"DBL_MAX", std::numeric_limits<double>::max()}
    };
    for (const auto& value : invalid) {
        checks.check("depth rejects " + value.first,
            jsoncons_set_max_depth(parser.value, value.second) != 0);
        checks.check("argument count rejects " + value.first,
            jsoncons_set_max_arg_num(parser.value, value.second) != 0);
        checks.success("rejected " + value.first + " preserves limits",
            parse(parser.value, "{\"a\":{\"b\":null,\"c\":true}}"));
        checks.failure("rejected " + value.first + " preserves depth ceiling",
            parse(parser.value, "[[[null]]]"));
        checks.failure("rejected " + value.first + " preserves count ceiling",
            parse(parser.value, "[null,true,false]"));
    }
    checks.check("depth accepts UINT_MAX", jsoncons_set_max_depth(parser.value, upper) == 0);
    checks.check("argument count accepts UINT_MAX", jsoncons_set_max_arg_num(parser.value, upper) == 0);
    checks.success("maximum representable limits permit ordinary JSON",
        parse(parser.value, "{\"a\":[null,true,false]}"));
}

void zero_limits(Checks& checks) {
    Parser parser;
    checks.check("set depth to zero", jsoncons_set_max_depth(parser.value, 0) == 0);
    checks.check("enable silence for zero depth", jsoncons_set_silence(parser.value, 1) == 0);
    for (const auto& text : {"null", "true", "false", "1", "\"value\""})
        checks.success(std::string("zero depth allows root ") + text, parse(parser.value, text));
    checks.failure("zero depth rejects object", parse(parser.value, "{}"));
    checks.failure("zero depth rejects array", parse(parser.value, "[]"));
    checks.check("restore depth for zero arguments", jsoncons_set_max_depth(parser.value, 3) == 0);
    checks.check("set arguments to zero", jsoncons_set_max_arg_num(parser.value, 0) == 0);
    for (const auto& text : {"{}", "[]", "{\"a\":[{},[]]}"})
        checks.success(std::string("zero arguments allows ") + text, parse(parser.value, text));
    for (const auto& text : {"null", "true", "false", "0", "\"\"", "{\"a\":null}", "[false]"})
        checks.failure(std::string("zero arguments rejects ") + text, parse(parser.value, text));
    checks.check("negative zero is an integral zero limit", jsoncons_set_max_depth(parser.value, -0.0) == 0);
    checks.failure("negative-zero depth has zero semantics", parse(parser.value, "{}"));
}

void boundaries_and_reuse(Checks& checks) {
    Parser parser;
    checks.check("set exact depth boundary", jsoncons_set_max_depth(parser.value, 3) == 0);
    checks.check("set exact count boundary", jsoncons_set_max_arg_num(parser.value, 2) == 0);
    checks.check("silence accepts any nonzero value", jsoncons_set_silence(parser.value, -7) == 0);
    checks.success("array exactly at depth", parse(parser.value, "[[[null]]]"));
    checks.failure("array above depth", parse(parser.value, "[[[[null]]]]"));
    checks.success("object exactly at depth", parse(parser.value, "{\"a\":{\"b\":{\"c\":true}}}"));
    checks.failure("object above depth", parse(parser.value, "{\"a\":{\"b\":{\"c\":{}}}}"));
    checks.success("null and bool exactly at count", parse(parser.value, "{\"first\":null,\"second\":true}"));
    checks.failure("null and bool above count", parse(parser.value, "{\"first\":null,\"second\":true,\"third\":false}"));
    checks.success("empty containers consume no arguments", parse(parser.value, "{\"a\":{},\"b\":[],\"c\":null,\"d\":false}"));
    checks.success("success resets count and path", parse(parser.value, "{\"new\":null,\"fresh\":false}"));
    const ParseResult malformed = parse(parser.value, "{\"leaked\":{\"a\":true,\"b\":");
    checks.failure("nested truncated input", malformed);
    checks.check("syntax error reports line", malformed.error.find("line ") != std::string::npos);
    checks.check("syntax error reports column", malformed.error.find("column ") != std::string::npos);
    checks.check("silent error produces no arguments", malformed.output.empty());
    checks.success("syntax failure resets count and path", parse(parser.value, "{\"fresh\":true}"));
    checks.check("disable silence after reused parser", jsoncons_set_silence(parser.value, 0) == 0);
    checks.success("output has fresh path after nested failure", parse(parser.value, "{\"fresh\":true}"), "fresh: true\n");
    checks.success("successful root array resets afterward", parse(parser.value, "[null,false]"), "array.array: \narray.array: false\n");
    checks.success("root scalar has fresh empty path", parse(parser.value, "2"), ": 2\n");
    const ParseResult count_error = parse(parser.value, "{\"a\":null,\"b\":true,\"c\":false}");
    checks.failure("retains count limit when unsilenced", count_error);
    checks.check("count limit prints exactly permitted prefix", count_error.output == "a: \nb: true\n");
    checks.success("count failure resets parser", parse(parser.value, "{\"after\":false}"), "after: false\n");
    checks.check("tighten depth retained across calls", jsoncons_set_max_depth(parser.value, 1) == 0);
    checks.failure("retains depth limit across calls", parse(parser.value, "{\"too\":[]}"));
    checks.success("depth failure resets parser", parse(parser.value, "{\"after\":null}"), "after: \n");
    checks.check("reenable silence", jsoncons_set_silence(parser.value, 2) == 0);
    checks.success("silence persists on successful reuse", parse(parser.value, "{\"quiet\":false}"));
    checks.failure("silence still validates malformed JSON", parse(parser.value, "{\"quiet\":true,}"));
    checks.success("silence persists after failed reuse", parse(parser.value, "{\"quiet\":true}"));
}

} // namespace

int main() {
    try {
        Checks checks;
        invalid_arguments(checks);
        limit_setters(checks);
        zero_limits(checks);
        boundaries_and_reuse(checks);
        return checks.finish();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: C API test setup: %s\n", error.what());
        return 1;
    }
}
#else
int main() {
    std::fprintf(stderr, "jsoncons C API tests require an enabled JSONCONS engine\n");
    return 77;
}
#endif
