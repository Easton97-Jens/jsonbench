// The CLI only accepts positive integral limits. Exercise both SAX adapters'
// public C APIs for zero limits, rejected setters, reuse, and binary-safe I/O.
#include "../src/rjparser.h"
#include "../src/nlparser.h"
#include "test_support.h"

#include <climits>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if HAVE_RAPIDJSON || HAVE_NLOHMANNJSON
namespace {
using namespace std::string_view_literals;

using jsonbench::test::CapturedOutput;
using jsonbench::test::ErrorSlot;
using jsonbench::test::TemporaryInput;
using jsonbench::test::TestError;
using jsonbench::test::parse_path;
template <typename Api>
using Parser = jsonbench::test::ApiParser<Api>;

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

using Result = jsonbench::test::ParseResult;

template <typename Api>
Result parse(typename Api::Handle* parser, std::string_view text, bool file = false) {
    if (file) {
        TemporaryInput input(text);
        return parse_path<Api>(parser, input.path());
    }
    ErrorSlot error;
    CapturedOutput output;
    const int status = Api::buffer(parser, text.data(), static_cast<unsigned int>(text.size()), error.slot());
    return {status, error.replaced(), !error.get(), error.get() ? error.get() : "", output.read()};
}

class Checks : public jsonbench::test::TestChecks {
  public:
    void success(std::string_view name, const Result& result, std::string_view expected = "") {
        success3(name, result, expected, {": succeeds", ": clears old diagnostic", ": byte-exact output"});
    }
    void failure(std::string_view name, const Result& result) {
        failure2(name, result, result.status != 0, {": fails", ": fresh malloc-owned diagnostic"});
    }
    int finish() const { return report("SAX adapter C APIs"); }
};

template <typename Api>
void invalid_arguments(Checks& checks) {
    {
        ErrorSlot error;
        checks.check("init rejects null parser slot", Api::init(nullptr, error.slot()) != 0);
        checks.check("invalid init clears old diagnostic", error.replaced() && !error.get());
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
        const unsigned int length = kind == 1 ? 0 : 1;
        const int rc = kind == 0 ? Api::buffer(nullptr, "{}", 2, error.slot()) :
            Api::buffer(parser.value, nullptr, length, error.slot());
        checks.check("invalid buffer arguments " + std::to_string(kind), rc != 0);
        checks.check("invalid buffer replaces diagnostic " + std::to_string(kind), error.replaced());
        checks.check("invalid buffer diagnostic is null or nonempty " + std::to_string(kind),
            !error.get() || *error.get());
    }
    for (const auto& [name, result] : std::vector<std::pair<std::string, Result>>{
            {"null parser", parse_path<Api>(nullptr, input.path())},
            {"null filename", parse_path<Api>(parser.value, nullptr)}}) {
        checks.check("file rejects " + name, result.status != 0);
        checks.check("file replaces diagnostic for " + name, result.replaced);
        checks.check("invalid file emits no output: " + name, result.output.empty());
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
    const auto upper = static_cast<double>(UINT_MAX);
    const std::vector<std::pair<std::string, double>> invalid = {
        {"NaN", std::numeric_limits<double>::quiet_NaN()},
        {"infinity", std::numeric_limits<double>::infinity()},
        {"negative infinity", -std::numeric_limits<double>::infinity()},
        {"negative integer", -1.0}, {"negative fraction", -0.5},
        {"fraction below one", 0.5}, {"positive fraction", 1.5},
        {"above UINT_MAX", upper + 1.0}
    };
    for (const auto& [name, value] : invalid) {
        checks.check("depth rejects " + name, Api::depth(parser.value, value) != 0);
        checks.check("argument count rejects " + name, Api::arguments(parser.value, value) != 0);
        checks.success("rejected " + name + " preserves limits", parse<Api>(parser.value, R"({"a":[null,true]})"));
        checks.failure("rejected " + name + " preserves depth ceiling", parse<Api>(parser.value, R"({"a":[[null]]})"));
        checks.failure("rejected " + name + " preserves argument ceiling", parse<Api>(parser.value, "[null,true,false]"));
    }
    checks.check("depth accepts UINT_MAX", Api::depth(parser.value, upper) == 0);
    checks.check("argument count accepts UINT_MAX", Api::arguments(parser.value, upper) == 0);
    checks.success("maximum limits permit ordinary JSON", parse<Api>(parser.value, R"({"a":[null,true,false]})"));
    checks.check("depth accepts zero", Api::depth(parser.value, 0) == 0);
    for (const auto& value : {"null", "true", "false", "1", R"("value")"})
        checks.success(std::string("zero depth allows root ") + value, parse<Api>(parser.value, value));
    for (const auto& value : {"{}", "[]"})
        checks.failure(std::string("zero depth rejects ") + value, parse<Api>(parser.value, value));
    checks.check("restore depth three", Api::depth(parser.value, 3) == 0);
    checks.check("argument count accepts zero", Api::arguments(parser.value, 0) == 0);
    for (const auto& value : {"{}", "[]", R"({"a":[{},[]]})"})
        checks.success(std::string("zero arguments allows ") + value, parse<Api>(parser.value, value));
    for (const auto& value : {"null", "true", "false", "0", R"("")", R"({"a":null})", "[false]"})
        checks.failure(std::string("zero arguments rejects ") + value, parse<Api>(parser.value, value));
    checks.check("restore arguments two", Api::arguments(parser.value, 2) == 0);
    checks.success("exact depth", parse<Api>(parser.value, "[[[null]]]"));
    checks.failure("above depth", parse<Api>(parser.value, "[[[[null]]]]"));
    checks.success("bool and null exactly at count", parse<Api>(parser.value, R"({"a":null,"b":false})"));
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
        const Result truncated = parse<Api>(parser.value, R"({"leaked":{"x":true,"y":)", file);
        checks.failure(kind + " truncated input", truncated);
        checks.check(kind + " emits completed prefix only", truncated.output == "leaked.x: true\n");
        checks.success(kind + " fresh path after failure", parse<Api>(parser.value, R"({"fresh":false})", file), "fresh: false\n");
        checks.success(kind + " root array", parse<Api>(parser.value, "[null,true]", file), "array.array: \narray.array: true\n");
        checks.success(kind + " root scalar resets array path", parse<Api>(parser.value, "2", file), ": 2\n");
        const Result count = parse<Api>(parser.value, "[null,true,false]", file);
        checks.failure(kind + " retained argument limit", count);
        checks.check(kind + " only permitted arguments printed", count.output == "array.array: \narray.array: true\n");
        checks.success(kind + " resets count after failure", parse<Api>(parser.value, R"({"a":true,"b":null})", file), "a: true\nb: \n");
        checks.failure(kind + " retained depth limit", parse<Api>(parser.value, "[[[null]]]", file));
        checks.success(kind + " resets after depth failure", parse<Api>(parser.value, R"({"after":null})", file), "after: \n");
        constexpr auto binary = "a\0b.c\0d: v\0x\na\0b.after: false\n"sv;
        checks.success(kind + " embedded NUL values and paths", parse<Api>(parser.value,
            R"({"a\u0000b":{"c\u0000d":"v\u0000x","after":false}})", file),
            std::string(binary));
        checks.success(kind + " full dotted parent restoration", parse<Api>(parser.value,
            R"({"a.b":{"c.d":1},"after":2})", file), "a.b.c.d: 1\nafter: 2\n");
        checks.success(kind + " EOF root number is finalized", parse<Api>(parser.value, "1.250e+3", file), ": 1.250e+3\n");
        checks.failure(kind + " empty input", parse<Api>(parser.value, "", file));
        checks.failure(kind + " truncated EOF number", parse<Api>(parser.value, "1e+", file));
    }
    checks.check("nonzero silence accepted", Api::silence(parser.value, -7) == 0);
    checks.success("silent parsing validates and suppresses output", parse<Api>(parser.value, R"({"a":null,"b":true})"));
    checks.failure("silence retains argument limit", parse<Api>(parser.value, "[null,true,false]"));
    checks.failure("silence retains syntax validation", parse<Api>(parser.value, R"({"a":true,})"));
    checks.success("silence persists across failed file reuse", parse<Api>(parser.value, R"({"quiet":false})", true));
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
            checks.check(std::string(label) + ": no silent output", result.output.empty());
        }
    }
    const std::string trailing = "{}" + std::string(16382, ' ') + "[]";
    checks.failure("trailing material after file buffer boundary", parse<Api>(parser.value, trailing, true));
    checks.failure("trailing material in buffer API", parse<Api>(parser.value, trailing));
    checks.check("disable silence for valid boundary output", Api::silence(parser.value, 0) == 0);
    const std::string unicode = std::string(16382, ' ') + "\"\xc3\xa4\xf0\x9f\x98\x80\"";
    checks.success("UTF-8 spans file buffer boundary", parse<Api>(parser.value, unicode, true), ": \xc3\xa4\xf0\x9f\x98\x80\n");
    const std::string escape = std::string(16382, ' ') + R"("\u0000x")";
    constexpr auto binary = ": \0x\n"sv;
    checks.success("decoded NUL escape spans file buffer boundary", parse<Api>(parser.value, escape, true),
        std::string(binary));
    checks.success("reused adapter after boundary failures", parse<Api>(parser.value, R"({"fresh":true})"), "fresh: true\n");
}

template <typename Api>
void run(Checks& checks) {
    checks.set_engine(Api::name());
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
