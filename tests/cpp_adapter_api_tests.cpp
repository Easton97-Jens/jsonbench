// File-based C API contracts for the native JsonCpp DOM and simdjson On-Demand
// adapters. These complement the common CLI matrix with zero limits, reuse,
// numeric capability differences, and bounded traversal with enlarged limits.
#include "../src/jsoncppparser.h"
#include "../src/simdjsonparser.h"
#include "test_support.h"

#include <array>
#include <climits>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>
#include <string_view>

#if HAVE_JSONCPP || HAVE_SIMDJSON
namespace {
using namespace std::string_view_literals;

using jsonbench::test::CapturedOutput;
using jsonbench::test::ErrorSlot;
using jsonbench::test::TemporaryInput;
using jsonbench::test::TestError;
using jsonbench::test::parse_path;
using jsonbench::test::parse_file;
template <typename Api>
using Parser = jsonbench::test::ApiParser<Api>;

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

using Result = jsonbench::test::ParseResult;

class Checks : public jsonbench::test::TestChecks {
  public:
    void success(std::string_view name, const Result& result, std::string_view expected = "") {
        success2(name, result, expected);
    }
    void failure(std::string_view name, const Result& result, std::string_view expected = "") {
        failure1(name, result, expected);
    }
    int finish() const { return report("C++ adapter C APIs"); }
};

template <typename Api>
void invalid_arguments(Checks& checks) {
    typename Api::Handle* handle = nullptr;
    ErrorSlot error;
    checks.check("null initialization slots", Api::init(nullptr, error.slot()) != 0 &&
        Api::init(&handle, nullptr) != 0 && !handle && Api::init(nullptr, nullptr) != 0);
    checks.check("null setters and cleanup", Api::depth(nullptr, 1) != 0 &&
        Api::arguments(nullptr, 1) != 0 && Api::silence(nullptr, 1) != 0 && Api::cleanup(nullptr) == 0);
    Parser<Api> parser;
    TemporaryInput input("true");
    const Result null_parser = parse_path<Api>(nullptr, input.path());
    const Result null_filename = parse_path<Api>(parser.value, nullptr);
    checks.check("invalid parser/filename reset diagnostics and emit nothing",
        null_parser.status != 0 && null_parser.replaced && null_parser.null_error && null_parser.output.empty() &&
        null_filename.status != 0 && null_filename.replaced && null_filename.null_error && null_filename.output.empty());
    checks.check("null diagnostic slot rejected", Api::file(parser.value, input.path(), nullptr) != 0);
    checks.failure("missing file", parse_path<Api>(parser.value, (std::string(input.path()) + ".missing").c_str()));
    checks.success("reuse after invalid API calls", parse_file<Api>(parser.value, "true"), ": true\n");
}

template <typename Api>
void limits(Checks& checks) {
    Parser<Api> parser;
    checks.check("configure two containers and arguments", Api::depth(parser.value, 2) == 0 &&
        Api::arguments(parser.value, 2) == 0 && Api::silence(parser.value, 1) == 0);
    const std::array invalid = {-1.0, -0.5, 0.5, 1.5,
        std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(), static_cast<double>(UINT_MAX) + 1};
    for (std::size_t i = 0; i < invalid.size(); ++i) {
        const std::string label = "invalid setter " + std::to_string(i);
        checks.check(label, Api::depth(parser.value, invalid[i]) != 0 && Api::arguments(parser.value, invalid[i]) != 0);
        checks.success(std::string(label) + " preserves exact limits", parse_file<Api>(parser.value, R"({"a":[true,null]})"));
        checks.failure(std::string(label) + " preserves depth ceiling", parse_file<Api>(parser.value, "[[[]]]"));
        checks.failure(std::string(label) + " preserves count ceiling", parse_file<Api>(parser.value, "[true,null,false]"));
    }
    checks.check("zero depth accepted", Api::depth(parser.value, -0.0) == 0);
    for (const char* value : {"true", "null", "42", R"("value")"})
        checks.success(std::string("root scalar at depth zero ") + value, parse_file<Api>(parser.value, value));
    for (const char* value : {"{}", "[]"})
        checks.failure(std::string("container at depth zero ") + value, parse_file<Api>(parser.value, value), "depth");
    checks.check("zero count accepted", Api::depth(parser.value, 3) == 0 && Api::arguments(parser.value, 0) == 0);
    checks.success("empty containers at count zero", parse_file<Api>(parser.value, R"({"a":[{},[]]})"));
    for (const char* value : {"true", "false", "null", "0", R"("")"})
        checks.failure(std::string("scalar at count zero ") + value, parse_file<Api>(parser.value, value), "Argument number");
    checks.check("restore exact argument limit", Api::arguments(parser.value, 3) == 0);
    checks.success("all scalar kinds counted exactly", parse_file<Api>(parser.value, "[[[null,true,false]]]"));
    checks.failure("four scalar leaves exceed count", parse_file<Api>(parser.value, "[null,true,false,0]"));
}

template <typename Api>
void reuse_and_numbers(Checks& checks) {
    Parser<Api> parser;
    checks.success("source order and structural path restoration", parse_file<Api>(parser.value,
        R"({"z":[{"b":true},null],"a.b":{"c":2},"after":false})"),
        "z.z.b: true\nz.z: \na.b.c: 2\nafter: false\n");
    constexpr auto binary = "a\0b: x\0y\n"sv;
    checks.success("embedded NUL name and value", parse_file<Api>(parser.value, R"({"a\u0000b":"x\u0000y"})"),
        std::string(binary));
    for (const char* invalid : {"[true,]", "1e+", R"("\udc00")"}) {
        checks.failure("syntax rejection", parse_file<Api>(parser.value, invalid));
        checks.success("clean state after syntax failure", parse_file<Api>(parser.value, R"({"fresh":false})"), "fresh: false\n");
    }
    const std::string finite = R"({"min":-9223372036854775808,"max":9223372036854775807,)"
        R"("u":18446744073709551615,"d":0.123456789012345678901234567890,"e":1.2300e+03,"n":-0})";
    checks.success("lossless numeric spelling", parse_file<Api>(parser.value, finite),
        "min: -9223372036854775808\nmax: 9223372036854775807\nu: 18446744073709551615\n"
        "d: 0.123456789012345678901234567890\ne: 1.2300e+03\nn: -0\n");
    for (const char* number : {"18446744073709551616", "-9223372036854775809", "1e9999"}) {
        if (Api::wide_numbers)
            checks.success("raw numeric range retained", parse_file<Api>(parser.value, number), std::string(": ") + number + '\n');
        else checks.failure("unsupported native numeric range rejected", parse_file<Api>(parser.value, number));
    }
    const Result duplicate = parse_file<Api>(parser.value, R"({"k":1,"k":2})");
    if (Api::duplicates) checks.success("duplicate keys preserved", duplicate, "k: 1\nk: 2\n");
    else checks.failure("duplicate keys explicitly rejected", duplicate, "Duplicate key");
    checks.check("nonzero silence accepted", Api::silence(parser.value, -7) == 0);
    checks.success("silence suppresses output", parse_file<Api>(parser.value, R"({"quiet":true})"));
    checks.failure("silence retains syntax validation", parse_file<Api>(parser.value, "{} []"));
    checks.failure("silence rejects trailing raw NUL", parse_file<Api>(parser.value, std::string("{}\0", 3)));
    checks.failure("oversized token rejected", parse_file<Api>(parser.value, R"(")" + std::string(JSON_STRING_SIZE, 'x') + R"(")"));
    checks.success("reuse after native and resource failures", parse_file<Api>(parser.value, "[true,null]"));
}

std::string nested_array_argument(unsigned int depth) {
    std::string output = "array";
    for (unsigned int i = 0; i < depth; ++i) output += ".array";
    return output + ": true\n";
}

template <typename Api>
void deep_case(Checks& checks, typename Api::Handle* parser,
        unsigned int depth, bool leaf, bool silent) {
    const std::string input = std::string(depth, '[') + (leaf ? "true" : "") + std::string(depth, ']');
    const std::string label = "depth " + std::to_string(depth) +
        (leaf ? " boolean leaf" : " empty arrays") + (silent ? " silent" : " output");
    const Result result = parse_file<Api>(parser, input);
    if (depth > Api::safe_depth) {
        checks.failure(label, result, Api::duplicates ? "depth" : "stackLimit");
        return;
    }
    const std::string expected = leaf && !silent ? nested_array_argument(depth) : "";
    checks.success(label, result, expected);
}

template <typename Api>
void large_depths(Checks& checks) {
    Parser<Api> parser;
    checks.check("enlarged integral limits accepted", Api::depth(parser.value, UINT_MAX) == 0 &&
        Api::arguments(parser.value, UINT_MAX) == 0);
    for (bool silent : {false, true}) {
        checks.check("set deep traversal mode", Api::silence(parser.value, silent) == 0);
        for (unsigned int depth : {500u, 501u, 512u, 513u, 999u, 2000u}) {
            for (bool leaf : {false, true})
                deep_case<Api>(checks, parser.value, depth, leaf, silent);
        }
    }
    checks.check("restore default depth and one argument", Api::depth(parser.value, 500) == 0 &&
        Api::arguments(parser.value, 1) == 0 && Api::silence(parser.value, 1) == 0);
    checks.success("default depth exact", parse_file<Api>(parser.value, std::string(500, '[') + "true" + std::string(500, ']')));
    checks.failure("default depth exceeded", parse_file<Api>(parser.value, std::string(501, '[') + "true" + std::string(501, ']')));
    checks.failure("boolean leaf consumes count", parse_file<Api>(parser.value, "[true,false]"));
    checks.success("parser reusable after deep failures", parse_file<Api>(parser.value, "true"));
}

template <typename Api>
void run(Checks& checks) {
    checks.set_engine(Api::name());
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
