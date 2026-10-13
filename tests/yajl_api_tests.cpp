// Public C API regressions, including boundaries unavailable through the CLI.
// Compile the YAJL adapter as C, then link this harness with the enabled YAJL.
#include "../src/yajlparser.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

#if HAVE_YAJL
namespace {

class TemporaryInput {
    std::string path_;
  public:
    explicit TemporaryInput(const std::string& text) {
        char name[] = "/tmp/jsonbench-yajl-api-XXXXXX";
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

// Callers own both their old diagnostic and each newly returned diagnostic.
// Starting with an allocation also catches APIs incorrectly freeing old errors.
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

class Parser {
  public:
    yajl_json_data* value = nullptr;
    Parser() {
        ErrorSlot error;
        if (yajl_json_init(&value, &error.value) != 1 || !value || error.value) {
            yajl_json_cleanup(value);
            throw std::runtime_error("parser initialization failed");
        }
    }
    ~Parser() { yajl_json_cleanup(value); }
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

Result parse_path(yajl_json_data* parser, const char* path) {
    ErrorSlot error;
    CapturedOutput output;
    const int status = yajl_parse_file(parser, path, &error.value);
    return {status, error.replaced(), !error.value,
        error.value ? error.value : "", output.read()};
}

Result parse(yajl_json_data* parser, const std::string& text) {
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
    void success(const std::string& name, const Result& result,
            const std::string& output = "") {
        check(name + ": succeeds", result.status == 0);
        check(name + ": clears old diagnostic", result.replaced && result.null_error);
        check(name + ": byte-exact output", result.output == output);
        if (result.status != 0)
            std::fprintf(stderr, "  diagnostic: %s\n", result.error.c_str());
    }
    void failure(const std::string& name, const Result& result) {
        check(name + ": fails", result.status == -1);
        check(name + ": fresh malloc-owned diagnostic",
            result.replaced && !result.null_error && !result.error.empty());
    }
    int finish() const {
        std::printf("YAJL C API: %u checks passed, %u failed\n", total_ - failures_, failures_);
        return failures_ ? 1 : 0;
    }
};

void invalid_arguments(Checks& checks) {
    {
        ErrorSlot error;
        checks.check("init rejects null parser slot", yajl_json_init(nullptr, &error.value) == -1);
        checks.check("invalid init clears old diagnostic", error.replaced() && !error.value);
    }
    yajl_json_data* parser_slot = nullptr;
    checks.check("init rejects null diagnostic slot", yajl_json_init(&parser_slot, nullptr) == -1);
    checks.check("invalid init leaves parser null", !parser_slot);
    checks.check("init rejects both null slots", yajl_json_init(nullptr, nullptr) == -1);
    checks.check("cleanup accepts null", yajl_json_cleanup(nullptr) == 1);
    Parser parser;
    TemporaryInput input("null");
    for (const auto& invalid : std::vector<std::pair<std::string, Result>>{
            {"null parser", parse_path(nullptr, input.path())},
            {"null filename", parse_path(parser.value, nullptr)}}) {
        checks.check("file parse rejects " + invalid.first, invalid.second.status == -1);
        checks.check("file parse clears diagnostic for " + invalid.first,
            invalid.second.replaced && invalid.second.null_error);
    }
    checks.check("file parse rejects null diagnostic slot",
        yajl_parse_file(parser.value, input.path(), nullptr) == -1);
    checks.failure("missing file", parse_path(parser.value, (std::string(input.path()) + ".missing").c_str()));
    {
        ErrorSlot error;
        checks.check("chunk rejects null parser", yajl_json_process_chunk(nullptr, "{}", 2, &error.value) == -1);
        checks.check("null parser chunk clears diagnostic", error.replaced() && !error.value);
    }
    {
        ErrorSlot error;
        checks.check("chunk rejects null nonempty buffer", yajl_json_process_chunk(parser.value, nullptr, 1, &error.value) == -1);
        checks.check("null buffer chunk clears diagnostic", error.replaced() && !error.value);
    }
    checks.check("chunk rejects null diagnostic slot", yajl_json_process_chunk(parser.value, "{}", 2, nullptr) == -1);
    checks.check("process alias rejects null diagnostic slot", yajl_json_process(parser.value, "{}", 2, nullptr) == -1);
    {
        ErrorSlot error;
        checks.check("complete rejects null parser", yajl_json_complete(nullptr, &error.value) == -1);
        checks.check("null parser complete clears diagnostic", error.replaced() && !error.value);
    }
    checks.check("complete rejects null diagnostic slot", yajl_json_complete(parser.value, nullptr) == -1);
    {
        ErrorSlot error;
        checks.check("empty null buffer is a valid chunk", yajl_json_process_chunk(parser.value, nullptr, 0, &error.value) == 1);
        checks.check("empty chunk clears diagnostic", error.replaced() && !error.value);
    }
    checks.success("parser usable after invalid calls", parse(parser.value, "null"), ": \n");
}

void split_case(Checks& checks, const std::string& name, const std::string& input,
        bool valid, const std::string& expected = "") {
    for (std::size_t split = 0; split <= input.size(); ++split) {
        const std::string label = name + " split " + std::to_string(split);
        Parser parser;
        parser.value->silence = valid ? 0 : 1;
        CapturedOutput output;
        bool failed = false;
        for (unsigned int part = 0; part < 2 && !failed; ++part) {
            const std::size_t offset = part ? split : 0;
            const std::size_t length = part ? input.size() - split : split;
            ErrorSlot error;
            // Exercise both public entry points with identical semantics.
            const int rc = part ? yajl_json_process(parser.value, input.data() + offset,
                static_cast<unsigned int>(length), &error.value) :
                yajl_json_process_chunk(parser.value, input.data() + offset,
                    static_cast<unsigned int>(length), &error.value);
            checks.check(label + ": chunk replaces old diagnostic", error.replaced());
            checks.check(label + ": chunk result and diagnostic agree",
                (rc == 1 && !error.value) || (rc == -1 && error.value && *error.value));
            failed = rc != 1;
        }
        if (!failed) {
            ErrorSlot error;
            const int rc = yajl_json_complete(parser.value, &error.value);
            checks.check(label + ": complete replaces old diagnostic", error.replaced());
            checks.check(label + ": complete result and diagnostic agree",
                (rc == 1 && !error.value) || (rc == -1 && error.value && *error.value));
            failed = rc != 1;
        }
        checks.check(label + ": expected acceptance", failed != valid);
        checks.check(label + ": byte-exact output", output.read() == (valid ? expected : ""));
        if (valid) {
            ErrorSlot complete_error;
            checks.check(label + ": rejects duplicate completion",
                yajl_json_complete(parser.value, &complete_error.value) == -1);
            checks.check(label + ": duplicate completion has fresh diagnostic",
                complete_error.replaced() && complete_error.value && *complete_error.value);
            ErrorSlot chunk_error;
            checks.check(label + ": rejects chunks after completion",
                yajl_json_process_chunk(parser.value, " ", 1, &chunk_error.value) == -1);
            checks.check(label + ": post-completion chunk has fresh diagnostic",
                chunk_error.replaced() && chunk_error.value && *chunk_error.value);
            checks.check(label + ": failed repeated calls do not emit more output", output.read() == expected);
        }
    }
}

void chunk_boundaries(Checks& checks) {
    split_case(checks, "empty object", "{}", true);
    split_case(checks, "empty array", "[]", true);
    split_case(checks, "root integer EOF", "18446744073709551616", true, ": 18446744073709551616\n");
    split_case(checks, "root exponent EOF", "-0.1200e+123", true, ": -0.1200e+123\n");
    split_case(checks, "raw multibyte UTF-8", "{\"\xc3\xa4\":\"\xe6\x97\xa5\xf0\x9f\x98\x80\"}", true,
        "\xc3\xa4: \xe6\x97\xa5\xf0\x9f\x98\x80\n");
    split_case(checks, "surrogate pair", "{\"emoji\":\"\\uD83D\\uDE00\"}", true, "emoji: \xf0\x9f\x98\x80\n");
    split_case(checks, "escaped quote and slash", "{\"quote\":\"a\\\"b\\\\c\"}", true, "quote: a\"b\\c\n");
    const char nul_output[] = "a\0b: x\0y\n";
    split_case(checks, "decoded NUL", "{\"a\\u0000b\":\"x\\u0000y\"}", true,
        std::string(nul_output, sizeof nul_output - 1));
    split_case(checks, "root and nested arrays", "[[null,true],{\"k\":false},2]", true,
        "array.array.array: \narray.array.array: true\narray.array.k: false\narray.array: 2\n");
    split_case(checks, "dotted parents and siblings", "{\"a.b\":[{\"c.d\":1},2],\"after\":3}", true,
        "a.b.a.b.c.d: 1\na.b.a.b: 2\nafter: 3\n");
    split_case(checks, "repeated and empty keys", "{\"\":{\"\":1},\"k\":2,\"k\":3}", true,
        ": 1\nk: 2\nk: 3\n");

    const std::vector<std::pair<std::string, std::string>> invalid = {
        {"empty input", ""}, {"whitespace only", " \r\n\t"},
        {"truncated object", "{\"a\":1"}, {"truncated nested array", "[[true]"},
        {"truncated string", "\"text"}, {"truncated number", "1e+"},
        {"trailing document", "{}[]"}, {"trailing garbage", "null x"},
        {"trailing comma", "[1,]"}, {"missing separator", "[1 2]"},
        {"invalid escape", "\"\\q\""}, {"high surrogate alone", "\"\\ud800\""},
        {"low surrogate alone", "\"\\udc00\""},
        {"high surrogate plus ordinary unit", "\"\\ud800\\u0041\""},
        {"high surrogate plus high unit", "\"\\ud800\\ud800\""},
        {"surrogate plus raw continuation", "\"\\ud800a\""},
        {"overlong UTF-8", std::string("\"\xc0\xaf\"", 4)},
        {"UTF-8 surrogate code point", std::string("\"\xed\xa0\x80\"", 5)},
        {"UTF-8 beyond U+10FFFF", std::string("\"\xf4\x90\x80\x80\"", 6)},
        {"bare continuation", std::string("\"\x80\"", 3)},
        {"truncated UTF-8", std::string("\"\xe2\x82", 3)},
        {"raw NUL in string", std::string("\"a\0b\"", 5)},
        {"raw NUL after document", std::string("{}\0", 3)}
    };
    for (const auto& test : invalid) split_case(checks, test.first, test.second, false);

    // Feed one byte per call as well as two chunks: lexer/guard state must span
    // several calls, rather than merely preserving a final partial token.
    Parser parser;
    CapturedOutput output;
    const std::string input = "{\"\xc3\xa4\":\"\\ud83d\\ude00\",\"n\":-0e+3}";
    for (std::size_t i = 0; i < input.size(); ++i) {
        ErrorSlot error;
        checks.check("byte-at-a-time chunk " + std::to_string(i),
            yajl_json_process_chunk(parser.value, input.data() + i, 1, &error.value) == 1 &&
            error.replaced() && !error.value);
    }
    ErrorSlot error;
    checks.check("byte-at-a-time completion", yajl_json_complete(parser.value, &error.value) == 1 &&
        error.replaced() && !error.value);
    checks.check("byte-at-a-time byte-exact output", output.read() == "\xc3\xa4: \xf0\x9f\x98\x80\nn: -0e+3\n");
}

void limits_and_reuse(Checks& checks) {
    Parser parser;
    parser.value->silence = 1;
    parser.value->depth_limit = 0;
    for (const auto& value : {"null", "true", "false", "1", "\"x\""})
        checks.success(std::string("zero depth allows primitive ") + value, parse(parser.value, value));
    for (const auto& value : {"{}", "[]"})
        checks.failure(std::string("zero depth rejects container ") + value, parse(parser.value, value));
    parser.value->depth_limit = 3;
    parser.value->arg_num_limit = 0;
    for (const auto& value : {"{}", "[]", "{\"a\":[{},[]]}"})
        checks.success(std::string("zero arguments allows empty containers ") + value, parse(parser.value, value));
    for (const auto& value : {"null", "true", "false", "0", "\"\"", "{\"a\":null}", "[false]"})
        checks.failure(std::string("zero arguments rejects value ") + value, parse(parser.value, value));
    parser.value->arg_num_limit = 2;
    checks.success("array exactly at depth limit", parse(parser.value, "[[[null]]]"));
    checks.failure("array above depth limit", parse(parser.value, "[[[[null]]]]"));
    checks.success("object exactly at depth limit", parse(parser.value, "{\"a\":{\"b\":{\"c\":true}}}"));
    checks.failure("object above depth limit", parse(parser.value, "{\"a\":{\"b\":{\"c\":{}}}}"));
    checks.success("bool and null exactly at argument limit", parse(parser.value, "{\"a\":null,\"b\":false}"));
    checks.failure("bool and null above argument limit", parse(parser.value, "[null,true,false]"));
    checks.success("empty containers consume no arguments", parse(parser.value, "{\"a\":{},\"b\":[],\"c\":null,\"d\":true}"));
    for (const auto& value : {"null", "true", "false", "0", "\"\""}) {
        checks.success(std::string("repeated scalar exactly at count: ") + value,
            parse(parser.value, std::string("[") + value + "," + value + "]"));
        checks.failure(std::string("repeated scalar above count: ") + value,
            parse(parser.value, std::string("[") + value + "," + value + "," + value + "]"));
    }
    const Result incomplete = parse(parser.value, "{\"leaked\":{\"x\":true,\"y\":");
    checks.failure("nested truncated input", incomplete);
    checks.check("silent malformed document has no argument output", incomplete.output.empty());
    checks.success("reuse after syntax failure", parse(parser.value, "{\"fresh\":true}"));
    checks.check("reuse preserves caller settings", parser.value->depth_limit == 3 &&
        parser.value->arg_num_limit == 2 && parser.value->silence == 1);
    parser.value->silence = 0;
    checks.success("fresh path after failed file", parse(parser.value, "{\"fresh\":true}"), "fresh: true\n");
    checks.success("root array path", parse(parser.value, "[null,false]"), "array.array: \narray.array: false\n");
    checks.success("root scalar clears preceding array path", parse(parser.value, "2"), ": 2\n");
    const Result count_failure = parse(parser.value, "{\"a\":null,\"b\":true,\"c\":false}");
    checks.failure("retained count limit while unsilenced", count_failure);
    checks.check("count failure emits only permitted arguments", count_failure.output == "a: \nb: true\n");
    checks.success("reuse after count failure", parse(parser.value, "{\"after\":false}"), "after: false\n");
    parser.value->depth_limit = 1;
    checks.failure("retained tight depth limit", parse(parser.value, "{\"a\":[]}"));
    checks.success("reuse after depth failure", parse(parser.value, "{\"after\":null}"), "after: \n");
    checks.failure("file reader finalizes root numeric syntax", parse(parser.value, "1e+"));
    checks.success("reuse after EOF syntax error", parse(parser.value, "-0.0"), ": -0.0\n");
    checks.failure("file reader rejects trailing content", parse(parser.value, "{} true"));
    checks.success("reuse after trailing content", parse(parser.value, "{}"));
    parser.value->silence = 5;
    checks.success("nonzero silence persists", parse(parser.value, "{\"quiet\":true}"));
    checks.failure("silent mode still validates", parse(parser.value, "{\"quiet\":true,}"));
    checks.success("silence persists after error", parse(parser.value, "{\"quiet\":false}"));

    // A file parse starts a fresh document even if callers previously used the
    // incremental API and left a partially consumed or failed document behind.
    {
        ErrorSlot error;
        checks.check("incremental parse rejected after finalized file",
            yajl_json_process_chunk(parser.value, "{}", 2, &error.value) == -1 &&
            error.replaced() && error.value && *error.value);
    }
    checks.success("file parse resets finalized incremental state", parse(parser.value, "{}"));
    {
        Parser incremental;
        incremental.value->silence = 1;
        incremental.value->depth_limit = 2;
        incremental.value->arg_num_limit = 1;
        const char unfinished[] = "{\"stale\":[";
        ErrorSlot chunk_error;
        checks.check("unfinished incremental document initially accepted",
            yajl_json_process_chunk(incremental.value, unfinished, sizeof unfinished - 1,
                &chunk_error.value) == 1 && chunk_error.replaced() && !chunk_error.value);
        ErrorSlot complete_error;
        checks.check("unfinished incremental document rejected on completion",
            yajl_json_complete(incremental.value, &complete_error.value) == -1 &&
            complete_error.replaced() && complete_error.value && *complete_error.value);
        checks.success("file parse resets failed incremental document",
            parse(incremental.value, "{\"fresh\":true}"));
        checks.check("incremental-to-file reset preserves settings",
            incremental.value->silence == 1 && incremental.value->depth_limit == 2 &&
            incremental.value->arg_num_limit == 1);
        checks.failure("reset still enforces incremental caller count limit",
            parse(incremental.value, "[null,true]"));
    }
}

void length_boundaries(Checks& checks) {
    Parser parser;
    const std::size_t limit = JSON_STRING_SIZE;
    const std::string key(limit - 1, 'k');
    const std::string value(limit - 1, 'v');
    checks.success("key exactly below byte limit", parse(parser.value, "{\"" + key + "\":null}"), key + ": \n");
    checks.failure("key at byte limit", parse(parser.value, "{\"" + key + "k\":null}"));
    checks.success("value exactly below byte limit", parse(parser.value, "{\"v\":\"" + value + "\"}"), "v: " + value + "\n");
    checks.failure("value at byte limit", parse(parser.value, "{\"v\":\"" + value + "v\"}"));
    const std::string parent((limit - 2) / 2, 'p');
    const std::string child(limit - 2 - parent.size(), 'c');
    checks.success("full path exactly below byte limit",
        parse(parser.value, "{\"" + parent + "\":{\"" + child + "\":1}}"), parent + "." + child + ": 1\n");
    checks.failure("full path at byte limit", parse(parser.value,
        "{\"" + parent + "\":{\"" + child + "c\":1}}"));
    checks.failure("oversized path entering empty container", parse(parser.value,
        "{\"" + parent + "\":{\"" + child + "c\":{}}}"));
    // A valid decoded token can span several file-reader chunks. Length limits
    // apply to decoded bytes, not to the longer escape spelling in the input.
    std::string escaped_key;
    std::string escaped_value;
    for (std::size_t i = 0; i < limit - 1; ++i) {
        escaped_key += "\\u006b";
        escaped_value += "\\u0076";
    }
    checks.success("escaped key below decoded byte limit across file chunks",
        parse(parser.value, "{\"" + escaped_key + "\":null}"), key + ": \n");
    checks.failure("escaped key at decoded byte limit",
        parse(parser.value, "{\"" + escaped_key + "\\u006b\":null}"));
    checks.success("escaped value below decoded byte limit across file chunks",
        parse(parser.value, "{\"v\":\"" + escaped_value + "\"}"), "v: " + value + "\n");
    checks.failure("escaped value at decoded byte limit",
        parse(parser.value, "{\"v\":\"" + escaped_value + "\\u0076\"}"));
    parser.value->silence = 1;
    checks.failure("silent key length still enforced", parse(parser.value, "{\"" + key + "k\":0}"));
    checks.failure("silent value length still enforced", parse(parser.value, "\"" + value + "v\""));
    checks.failure("oversized token guard", parse(parser.value, "\"" + std::string(limit * 8 + 32, 'x') + "\""));
    checks.failure("oversized number guard", parse(parser.value, std::string(limit * 8 + 32, '1')));
    checks.success("reuse after length errors", parse(parser.value, "{\"fresh\":true}"));
    parser.value->silence = 0;
    const char expected[] = "a\0b.c\0d: v\0x\na\0b.after: false\nlast: \n";
    checks.success("NUL parents restore with explicit lengths", parse(parser.value,
        "{\"a\\u0000b\":{\"c\\u0000d\":\"v\\u0000x\",\"after\":false},\"last\":null}"),
        std::string(expected, sizeof expected - 1));
    checks.success("dotted keys are not structural pop delimiters", parse(parser.value,
        "{\"a.b\":{\"c.d\":1},\"after\":2}"), "a.b.c.d: 1\nafter: 2\n");
}

} // namespace

int main() {
    try {
        Checks checks;
        invalid_arguments(checks);
        chunk_boundaries(checks);
        limits_and_reuse(checks);
        length_boundaries(checks);
        return checks.finish();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: YAJL C API test setup: %s\n", error.what());
        return 1;
    }
}
#else
int main() {
    std::fprintf(stderr, "YAJL C API tests require an enabled YAJL engine\n");
    return 77;
}
#endif
