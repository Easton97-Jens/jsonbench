// Direct regression checks for the C interface, including parser reuse and
// limits that cannot be represented by the CLI's positive-integer options.
#include "../src/jsonconsparser.h"
#include "test_support.h"

#include <climits>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if HAVE_JSONCONS
namespace {

using jsonbench::test::CapturedOutput;
using jsonbench::test::ErrorSlot;
using jsonbench::test::TemporaryInput;
using jsonbench::test::TestError;

class Parser {
  public:
    jsoncons_parser* value = nullptr;
    Parser() {
        ErrorSlot error;
        const int rc = jsoncons_json_init(&value, error.slot());
        if (rc != 0 || value == nullptr || error.get() != nullptr) {
            jsoncons_json_cleanup(value);
            throw TestError("parser initialization failed");
        }
    }
    ~Parser() { jsoncons_json_cleanup(value); }
    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;
};

using ParseResult = jsonbench::test::ParseResult;

ParseResult parse_path(jsoncons_parser* parser, const char* path) {
    ErrorSlot error;
    CapturedOutput output;
    const int status = jsoncons_parse_file(parser, path, error.slot());
    return {status, error.replaced(), error.get() == nullptr,
        error.get() != nullptr ? error.get() : "", output.read()};
}

ParseResult parse(jsoncons_parser* parser, std::string_view text) {
    TemporaryInput input(text);
    return parse_path(parser, input.path());
}

class Checks : public jsonbench::test::TestChecks {
  public:
    void success(std::string_view name, const ParseResult& result, std::string_view expected = "") {
        success3(name, result, expected, {": succeeds", ": clears previous error", ": exact output"});
    }
    void failure(std::string_view name, const ParseResult& result) {
        failure2(name, result, result.status != 0, {": fails", ": returns fresh malloc-owned diagnostic"});
    }
    int finish() const { return report("jsoncons C API"); }
};

void invalid_arguments(Checks& checks) {
    {
        ErrorSlot error;
        checks.check("init rejects null parser slot", jsoncons_json_init(nullptr, error.slot()) != 0);
        checks.check("failed init clears previous error", error.replaced() && error.get() == nullptr);
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
    checks.check("invalid parser clears previous error", no_parser.replaced && no_parser.null_error);
    const ParseResult no_filename = parse_path(parser.value, nullptr);
    checks.check("parse rejects null filename", no_filename.status != 0);
    checks.check("invalid filename clears previous error", no_filename.replaced && no_filename.null_error);
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
    const auto upper = static_cast<double>(UINT_MAX);
    const std::vector<std::pair<std::string, double>> invalid = {
        {"NaN", std::numeric_limits<double>::quiet_NaN()},
        {"positive infinity", std::numeric_limits<double>::infinity()},
        {"negative infinity", -std::numeric_limits<double>::infinity()},
        {"negative integer", -1.0}, {"negative fraction", -0.5},
        {"fraction below one", 0.5}, {"positive fraction", 1.5},
        {"above UINT_MAX", upper + 1.0}, {"DBL_MAX", std::numeric_limits<double>::max()}
    };
    for (const auto& [name, value] : invalid) {
        checks.check("depth rejects " + name,
            jsoncons_set_max_depth(parser.value, value) != 0);
        checks.check("argument count rejects " + name,
            jsoncons_set_max_arg_num(parser.value, value) != 0);
        checks.success("rejected " + name + " preserves limits",
            parse(parser.value, R"({"a":{"b":null,"c":true}})"));
        checks.failure("rejected " + name + " preserves depth ceiling",
            parse(parser.value, "[[[null]]]"));
        checks.failure("rejected " + name + " preserves count ceiling",
            parse(parser.value, "[null,true,false]"));
    }
    checks.check("depth accepts UINT_MAX", jsoncons_set_max_depth(parser.value, upper) == 0);
    checks.check("argument count accepts UINT_MAX", jsoncons_set_max_arg_num(parser.value, upper) == 0);
    checks.success("maximum representable limits permit ordinary JSON",
        parse(parser.value, R"({"a":[null,true,false]})"));
}

void zero_limits(Checks& checks) {
    Parser parser;
    checks.check("set depth to zero", jsoncons_set_max_depth(parser.value, 0) == 0);
    checks.check("enable silence for zero depth", jsoncons_set_silence(parser.value, 1) == 0);
    for (const auto& text : {"null", "true", "false", "1", R"("value")"})
        checks.success(std::string("zero depth allows root ") + text, parse(parser.value, text));
    checks.failure("zero depth rejects object", parse(parser.value, "{}"));
    checks.failure("zero depth rejects array", parse(parser.value, "[]"));
    checks.check("restore depth for zero arguments", jsoncons_set_max_depth(parser.value, 3) == 0);
    checks.check("set arguments to zero", jsoncons_set_max_arg_num(parser.value, 0) == 0);
    for (const auto& text : {"{}", "[]", R"({"a":[{},[]]})"})
        checks.success(std::string("zero arguments allows ") + text, parse(parser.value, text));
    for (const auto& text : {"null", "true", "false", "0", R"("")", R"({"a":null})", "[false]"})
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
    checks.success("object exactly at depth", parse(parser.value, R"({"a":{"b":{"c":true}}})"));
    checks.failure("object above depth", parse(parser.value, R"({"a":{"b":{"c":{}}}})"));
    checks.success("null and bool exactly at count", parse(parser.value, R"({"first":null,"second":true})"));
    checks.failure("null and bool above count", parse(parser.value, R"({"first":null,"second":true,"third":false})"));
    checks.success("empty containers consume no arguments", parse(parser.value, R"({"a":{},"b":[],"c":null,"d":false})"));
    checks.success("success resets count and path", parse(parser.value, R"({"new":null,"fresh":false})"));
    const ParseResult malformed = parse(parser.value, R"({"leaked":{"a":true,"b":)");
    checks.failure("nested truncated input", malformed);
    checks.check("syntax error reports line", malformed.error.find("line ") != std::string::npos);
    checks.check("syntax error reports column", malformed.error.find("column ") != std::string::npos);
    checks.check("silent error produces no arguments", malformed.output.empty());
    checks.success("syntax failure resets count and path", parse(parser.value, R"({"fresh":true})"));
    checks.check("disable silence after reused parser", jsoncons_set_silence(parser.value, 0) == 0);
    checks.success("output has fresh path after nested failure", parse(parser.value, R"({"fresh":true})"), "fresh: true\n");
    checks.success("successful root array resets afterward", parse(parser.value, "[null,false]"), "array.array: \narray.array: false\n");
    checks.success("root scalar has fresh empty path", parse(parser.value, "2"), ": 2\n");
    const ParseResult count_error = parse(parser.value, R"({"a":null,"b":true,"c":false})");
    checks.failure("retains count limit when unsilenced", count_error);
    checks.check("count limit prints exactly permitted prefix", count_error.output == "a: \nb: true\n");
    checks.success("count failure resets parser", parse(parser.value, R"({"after":false})"), "after: false\n");
    checks.check("tighten depth retained across calls", jsoncons_set_max_depth(parser.value, 1) == 0);
    checks.failure("retains depth limit across calls", parse(parser.value, R"({"too":[]})"));
    checks.success("depth failure resets parser", parse(parser.value, R"({"after":null})"), "after: \n");
    checks.check("reenable silence", jsoncons_set_silence(parser.value, 2) == 0);
    checks.success("silence persists on successful reuse", parse(parser.value, R"({"quiet":false})"));
    checks.failure("silence still validates malformed JSON", parse(parser.value, R"({"quiet":true,})"));
    checks.success("silence persists after failed reuse", parse(parser.value, R"({"quiet":true})"));
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
