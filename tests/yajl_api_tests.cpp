// Public C API regressions, including boundaries unavailable through the CLI.
// Compile the YAJL adapter as C, then link this harness with the enabled YAJL.
#include "../src/yajlparser.h"
#include "test_support.h"

#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if HAVE_YAJL
namespace {
using namespace std::string_view_literals;

using jsonbench::test::CapturedOutput;
using jsonbench::test::ErrorSlot;
using jsonbench::test::TemporaryInput;
using jsonbench::test::TestError;

class Parser {
  public:
    yajl_json_data* value = nullptr;
    Parser() {
        ErrorSlot error;
        if (yajl_json_init(&value, error.slot()) != 1 || !value || error.get()) {
            yajl_json_cleanup(value);
            throw TestError("parser initialization failed");
        }
    }
    ~Parser() { yajl_json_cleanup(value); }
    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;
};

using Result = jsonbench::test::ParseResult;

Result parse_path(yajl_json_data* parser, const char* path) {
    ErrorSlot error;
    CapturedOutput output;
    const int status = yajl_parse_file(parser, path, error.slot());
    return {status, error.replaced(), !error.get(),
        error.get() ? error.get() : "", output.read()};
}

Result parse(yajl_json_data* parser, std::string_view text) {
    TemporaryInput input(text);
    return parse_path(parser, input.path());
}

class Checks : public jsonbench::test::TestChecks {
  public:
    void success(std::string_view name, const Result& result, std::string_view expected = "") {
        success3(name, result, expected, {": succeeds", ": clears old diagnostic", ": byte-exact output"});
    }
    void failure(std::string_view name, const Result& result) {
        failure2(name, result, result.status == -1, {": fails", ": fresh malloc-owned diagnostic"});
    }
    int finish() const { return report("YAJL C API"); }
};

void invalid_arguments(Checks& checks) {
    {
        ErrorSlot error;
        checks.check("init rejects null parser slot", yajl_json_init(nullptr, error.slot()) == -1);
        checks.check("invalid init clears old diagnostic", error.replaced() && !error.get());
    }
    yajl_json_data* parser_slot = nullptr;
    checks.check("init rejects null diagnostic slot", yajl_json_init(&parser_slot, nullptr) == -1);
    checks.check("invalid init leaves parser null", !parser_slot);
    checks.check("init rejects both null slots", yajl_json_init(nullptr, nullptr) == -1);
    checks.check("cleanup accepts null", yajl_json_cleanup(nullptr) == 1);
    Parser parser;
    TemporaryInput input("null");
    for (const auto& [name, result] : std::vector<std::pair<std::string, Result>>{
            {"null parser", parse_path(nullptr, input.path())},
            {"null filename", parse_path(parser.value, nullptr)}}) {
        checks.check("file parse rejects " + name, result.status == -1);
        checks.check("file parse clears diagnostic for " + name,
            result.replaced && result.null_error);
    }
    checks.check("file parse rejects null diagnostic slot",
        yajl_parse_file(parser.value, input.path(), nullptr) == -1);
    checks.failure("missing file", parse_path(parser.value, (std::string(input.path()) + ".missing").c_str()));
    {
        ErrorSlot error;
        checks.check("chunk rejects null parser", yajl_json_process_chunk(nullptr, "{}", 2, error.slot()) == -1);
        checks.check("null parser chunk clears diagnostic", error.replaced() && !error.get());
    }
    {
        ErrorSlot error;
        checks.check("chunk rejects null nonempty buffer", yajl_json_process_chunk(parser.value, nullptr, 1, error.slot()) == -1);
        checks.check("null buffer chunk clears diagnostic", error.replaced() && !error.get());
    }
    checks.check("chunk rejects null diagnostic slot", yajl_json_process_chunk(parser.value, "{}", 2, nullptr) == -1);
    checks.check("process alias rejects null diagnostic slot", yajl_json_process(parser.value, "{}", 2, nullptr) == -1);
    {
        ErrorSlot error;
        checks.check("complete rejects null parser", yajl_json_complete(nullptr, error.slot()) == -1);
        checks.check("null parser complete clears diagnostic", error.replaced() && !error.get());
    }
    checks.check("complete rejects null diagnostic slot", yajl_json_complete(parser.value, nullptr) == -1);
    {
        ErrorSlot error;
        checks.check("empty null buffer is a valid chunk", yajl_json_process_chunk(parser.value, nullptr, 0, error.slot()) == 1);
        checks.check("empty chunk clears diagnostic", error.replaced() && !error.get());
    }
    checks.success("parser usable after invalid calls", parse(parser.value, "null"), ": \n");
}

void split_case(Checks& checks, std::string_view name, std::string_view input,
        bool valid, std::string_view expected = "") {
    for (std::size_t split = 0; split <= input.size(); ++split) {
        const std::string label = std::string(name) + " split " + std::to_string(split);
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
                static_cast<unsigned int>(length), error.slot()) :
                yajl_json_process_chunk(parser.value, input.data() + offset,
                    static_cast<unsigned int>(length), error.slot());
            checks.check(std::string(label) + ": chunk replaces old diagnostic", error.replaced());
            checks.check(std::string(label) + ": chunk result and diagnostic agree",
                (rc == 1 && !error.get()) || (rc == -1 && error.get() && *error.get()));
            failed = rc != 1;
        }
        if (!failed) {
            ErrorSlot error;
            const int rc = yajl_json_complete(parser.value, error.slot());
            checks.check(std::string(label) + ": complete replaces old diagnostic", error.replaced());
            checks.check(std::string(label) + ": complete result and diagnostic agree",
                (rc == 1 && !error.get()) || (rc == -1 && error.get() && *error.get()));
            failed = rc != 1;
        }
        checks.check(std::string(label) + ": expected acceptance", failed != valid);
        checks.check(std::string(label) + ": byte-exact output", output.read() == (valid ? expected : ""));
        if (valid) {
            ErrorSlot complete_error;
            checks.check(std::string(label) + ": rejects duplicate completion",
                yajl_json_complete(parser.value, complete_error.slot()) == -1);
            checks.check(std::string(label) + ": duplicate completion has fresh diagnostic",
                complete_error.replaced() && complete_error.get() && *complete_error.get());
            ErrorSlot chunk_error;
            checks.check(std::string(label) + ": rejects chunks after completion",
                yajl_json_process_chunk(parser.value, " ", 1, chunk_error.slot()) == -1);
            checks.check(std::string(label) + ": post-completion chunk has fresh diagnostic",
                chunk_error.replaced() && chunk_error.get() && *chunk_error.get());
            checks.check(std::string(label) + ": failed repeated calls do not emit more output", output.read() == expected);
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
    split_case(checks, "surrogate pair", R"({"emoji":"\uD83D\uDE00"})", true, "emoji: \xf0\x9f\x98\x80\n");
    split_case(checks, "escaped quote and slash", R"({"quote":"a\"b\\c"})", true, "quote: a\"b\\c\n");
    constexpr auto nul_output = "a\0b: x\0y\n"sv;
    split_case(checks, "decoded NUL", R"({"a\u0000b":"x\u0000y"})", true,
        std::string(nul_output));
    split_case(checks, "root and nested arrays", R"([[null,true],{"k":false},2])", true,
        "array.array.array: \narray.array.array: true\narray.array.k: false\narray.array: 2\n");
    split_case(checks, "dotted parents and siblings", R"({"a.b":[{"c.d":1},2],"after":3})", true,
        "a.b.a.b.c.d: 1\na.b.a.b: 2\nafter: 3\n");
    split_case(checks, "repeated and empty keys", R"({"":{"":1},"k":2,"k":3})", true,
        ": 1\nk: 2\nk: 3\n");

    const std::vector<std::pair<std::string, std::string>> invalid = {
        {"empty input", ""}, {"whitespace only", " \r\n\t"},
        {"truncated object", R"({"a":1)"}, {"truncated nested array", "[[true]"},
        {"truncated string", R"("text)"}, {"truncated number", "1e+"},
        {"trailing document", "{}[]"}, {"trailing garbage", "null x"},
        {"trailing comma", "[1,]"}, {"missing separator", "[1 2]"},
        {"invalid escape", R"("\q")"}, {"high surrogate alone", R"("\ud800")"},
        {"low surrogate alone", R"("\udc00")"},
        {"high surrogate plus ordinary unit", R"("\ud800\u0041")"},
        {"high surrogate plus high unit", R"("\ud800\ud800")"},
        {"surrogate plus raw continuation", R"("\ud800a")"},
        {"overlong UTF-8", std::string("\"\xc0\xaf\"", 4)},
        {"UTF-8 surrogate code point", std::string("\"\xed\xa0\x80\"", 5)},
        {"UTF-8 beyond U+10FFFF", std::string("\"\xf4\x90\x80\x80\"", 6)},
        {"bare continuation", std::string("\"\x80\"", 3)},
        {"truncated UTF-8", std::string("\"\xe2\x82", 3)},
        {"raw NUL in string", std::string("\"a\0b\"", 5)},
        {"raw NUL after document", std::string("{}\0", 3)}
    };
    for (const auto& [name, input] : invalid) split_case(checks, name, input, false);

    // Feed one byte per call as well as two chunks: lexer/guard state must span
    // several calls, rather than merely preserving a final partial token.
    Parser parser;
    CapturedOutput output;
    const std::string input = "{\"\xc3\xa4\":\"\\ud83d\\ude00\",\"n\":-0e+3}";
    for (std::size_t i = 0; i < input.size(); ++i) {
        ErrorSlot error;
        checks.check("byte-at-a-time chunk " + std::to_string(i),
            yajl_json_process_chunk(parser.value, input.data() + i, 1, error.slot()) == 1 &&
            error.replaced() && !error.get());
    }
    ErrorSlot error;
    checks.check("byte-at-a-time completion", yajl_json_complete(parser.value, error.slot()) == 1 &&
        error.replaced() && !error.get());
    checks.check("byte-at-a-time byte-exact output", output.read() == "\xc3\xa4: \xf0\x9f\x98\x80\nn: -0e+3\n");
}

void limits_and_reuse(Checks& checks) {
    Parser parser;
    parser.value->silence = 1;
    parser.value->depth_limit = 0;
    for (const auto& value : {"null", "true", "false", "1", R"("x")"})
        checks.success(std::string("zero depth allows primitive ") + value, parse(parser.value, value));
    for (const auto& value : {"{}", "[]"})
        checks.failure(std::string("zero depth rejects container ") + value, parse(parser.value, value));
    parser.value->depth_limit = 3;
    parser.value->arg_num_limit = 0;
    for (const auto& value : {"{}", "[]", R"({"a":[{},[]]})"})
        checks.success(std::string("zero arguments allows empty containers ") + value, parse(parser.value, value));
    for (const auto& value : {"null", "true", "false", "0", R"("")", R"({"a":null})", "[false]"})
        checks.failure(std::string("zero arguments rejects value ") + value, parse(parser.value, value));
    parser.value->arg_num_limit = 2;
    checks.success("array exactly at depth limit", parse(parser.value, "[[[null]]]"));
    checks.failure("array above depth limit", parse(parser.value, "[[[[null]]]]"));
    checks.success("object exactly at depth limit", parse(parser.value, R"({"a":{"b":{"c":true}}})"));
    checks.failure("object above depth limit", parse(parser.value, R"({"a":{"b":{"c":{}}}})"));
    checks.success("bool and null exactly at argument limit", parse(parser.value, R"({"a":null,"b":false})"));
    checks.failure("bool and null above argument limit", parse(parser.value, "[null,true,false]"));
    checks.success("empty containers consume no arguments", parse(parser.value, R"({"a":{},"b":[],"c":null,"d":true})"));
    for (const auto& value : {"null", "true", "false", "0", R"("")"}) {
        checks.success(std::string("repeated scalar exactly at count: ") + value,
            parse(parser.value, std::string("[") + value + "," + value + "]"));
        checks.failure(std::string("repeated scalar above count: ") + value,
            parse(parser.value, std::string("[") + value + "," + value + "," + value + "]"));
    }
    const Result incomplete = parse(parser.value, R"({"leaked":{"x":true,"y":)");
    checks.failure("nested truncated input", incomplete);
    checks.check("silent malformed document has no argument output", incomplete.output.empty());
    checks.success("reuse after syntax failure", parse(parser.value, R"({"fresh":true})"));
    checks.check("reuse preserves caller settings", parser.value->depth_limit == 3 &&
        parser.value->arg_num_limit == 2 && parser.value->silence == 1);
    parser.value->silence = 0;
    checks.success("fresh path after failed file", parse(parser.value, R"({"fresh":true})"), "fresh: true\n");
    checks.success("root array path", parse(parser.value, "[null,false]"), "array.array: \narray.array: false\n");
    checks.success("root scalar clears preceding array path", parse(parser.value, "2"), ": 2\n");
    const Result count_failure = parse(parser.value, R"({"a":null,"b":true,"c":false})");
    checks.failure("retained count limit while unsilenced", count_failure);
    checks.check("count failure emits only permitted arguments", count_failure.output == "a: \nb: true\n");
    checks.success("reuse after count failure", parse(parser.value, R"({"after":false})"), "after: false\n");
    parser.value->depth_limit = 1;
    checks.failure("retained tight depth limit", parse(parser.value, R"({"a":[]})"));
    checks.success("reuse after depth failure", parse(parser.value, R"({"after":null})"), "after: \n");
    checks.failure("file reader finalizes root numeric syntax", parse(parser.value, "1e+"));
    checks.success("reuse after EOF syntax error", parse(parser.value, "-0.0"), ": -0.0\n");
    checks.failure("file reader rejects trailing content", parse(parser.value, "{} true"));
    checks.success("reuse after trailing content", parse(parser.value, "{}"));
    parser.value->silence = 5;
    checks.success("nonzero silence persists", parse(parser.value, R"({"quiet":true})"));
    checks.failure("silent mode still validates", parse(parser.value, R"({"quiet":true,})"));
    checks.success("silence persists after error", parse(parser.value, R"({"quiet":false})"));

    // A file parse starts a fresh document even if callers previously used the
    // incremental API and left a partially consumed or failed document behind.
    {
        ErrorSlot error;
        checks.check("incremental parse rejected after finalized file",
            yajl_json_process_chunk(parser.value, "{}", 2, error.slot()) == -1 &&
            error.replaced() && error.get() && *error.get());
    }
    checks.success("file parse resets finalized incremental state", parse(parser.value, "{}"));
    {
        Parser incremental;
        incremental.value->silence = 1;
        incremental.value->depth_limit = 2;
        incremental.value->arg_num_limit = 1;
        constexpr auto unfinished = R"({"stale":[)"sv;
        ErrorSlot chunk_error;
        checks.check("unfinished incremental document initially accepted",
            yajl_json_process_chunk(incremental.value, unfinished.data(), static_cast<unsigned int>(unfinished.size()),
                chunk_error.slot()) == 1 && chunk_error.replaced() && !chunk_error.get());
        ErrorSlot complete_error;
        checks.check("unfinished incremental document rejected on completion",
            yajl_json_complete(incremental.value, complete_error.slot()) == -1 &&
            complete_error.replaced() && complete_error.get() && *complete_error.get());
        checks.success("file parse resets failed incremental document",
            parse(incremental.value, R"({"fresh":true})"));
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
    checks.success("key exactly below byte limit", parse(parser.value, R"({")" + key + R"(":null})"), key + ": \n");
    checks.failure("key at byte limit", parse(parser.value, R"({")" + key + R"(k":null})"));
    checks.success("value exactly below byte limit", parse(parser.value, R"({"v":")" + value + R"("})"), "v: " + value + "\n");
    checks.failure("value at byte limit", parse(parser.value, R"({"v":")" + value + R"(v"})"));
    const std::string parent((limit - 2) / 2, 'p');
    const std::string child(limit - 2 - parent.size(), 'c');
    checks.success("full path exactly below byte limit",
        parse(parser.value, R"({")" + parent + R"(":{")" + child + R"(":1}})"), parent + "." + child + ": 1\n");
    checks.failure("full path at byte limit", parse(parser.value,
        R"({")" + parent + R"(":{")" + child + R"(c":1}})"));
    checks.failure("oversized path entering empty container", parse(parser.value,
        R"({")" + parent + R"(":{")" + child + R"(c":{}}})"));
    // A valid decoded token can span several file-reader chunks. Length limits
    // apply to decoded bytes, not to the longer escape spelling in the input.
    std::string escaped_key;
    std::string escaped_value;
    for (std::size_t i = 0; i < limit - 1; ++i) {
        escaped_key += R"(\u006b)";
        escaped_value += R"(\u0076)";
    }
    checks.success("escaped key below decoded byte limit across file chunks",
        parse(parser.value, R"({")" + escaped_key + R"(":null})"), key + ": \n");
    checks.failure("escaped key at decoded byte limit",
        parse(parser.value, R"({")" + escaped_key + R"(\u006b":null})"));
    checks.success("escaped value below decoded byte limit across file chunks",
        parse(parser.value, R"({"v":")" + escaped_value + R"("})"), "v: " + value + "\n");
    checks.failure("escaped value at decoded byte limit",
        parse(parser.value, R"({"v":")" + escaped_value + R"(\u0076"})"));
    parser.value->silence = 1;
    checks.failure("silent key length still enforced", parse(parser.value, R"({")" + key + R"(k":0})"));
    checks.failure("silent value length still enforced", parse(parser.value, R"(")" + value + R"(v")"));
    checks.failure("oversized token guard", parse(parser.value, R"(")" + std::string(limit * 8 + 32, 'x') + R"(")"));
    checks.failure("oversized number guard", parse(parser.value, std::string(limit * 8 + 32, '1')));
    checks.success("reuse after length errors", parse(parser.value, R"({"fresh":true})"));
    parser.value->silence = 0;
    constexpr auto expected = "a\0b.c\0d: v\0x\na\0b.after: false\nlast: \n"sv;
    checks.success("NUL parents restore with explicit lengths", parse(parser.value,
        R"({"a\u0000b":{"c\u0000d":"v\u0000x","after":false},"last":null})"),
        std::string(expected));
    checks.success("dotted keys are not structural pop delimiters", parse(parser.value,
        R"({"a.b":{"c.d":1},"after":2})"), "a.b.c.d: 1\nafter: 2\n");
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
