// Public path-builder contracts shared by SAX adapters. These tests do not
// depend on private stack layout or one particular parser implementation.
#include "../src/pathhandler.h"
#include "test_support.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

namespace {
class Checks : public jsonbench::test::TestChecks {
  public:
    void path(std::string_view label, PathHandler& handler, std::string_view expected) {
        check(std::string(label) + ": byte-exact name", handler.name() == expected);
        check(std::string(label) + ": no path error", !handler.m_error);
        // Both NUL terminators and bytes beyond the declared destination size
        // matter: oversized writes must not corrupt adjacent memory.
        std::vector<char> buffer(JSON_STRING_SIZE + 16, '#');
        handler.buildArgName(buffer.data() + 8);
        check(std::string(label) + ": legacy output bytes", std::memcmp(buffer.data() + 8,
            expected.data(), expected.size()) == 0);
        check(std::string(label) + ": legacy output terminator", buffer[8 + expected.size()] == '\0');
        bool canaries = true;
        for (unsigned int i = 0; i < 8; ++i)
            canaries = canaries && buffer[i] == '#' && buffer[JSON_STRING_SIZE + 8 + i] == '#';
        check(std::string(label) + ": destination canaries unchanged", canaries);
    }
    int finish() const { return report("PathHandler"); }
};

void structural_paths(Checks& checks) {
    PathHandler handler;
    checks.path("root scalar has empty name", handler, "");
    handler.push(); // Root object.
    handler.setKey("a.b", 3);
    handler.push();
    handler.setKey("c.d", 3);
    checks.path("dots inside both keys", handler, "a.b.c.d");
    handler.push();
    handler.setKey("leaf", 4);
    checks.path("three levels", handler, "a.b.c.d.leaf");
    handler.pop();
    checks.path("pop restores full dotted parent key", handler, "a.b.c.d");
    handler.setKey("next", 4);
    checks.path("nested sibling after dotted key", handler, "a.b.next");
    handler.pop();
    checks.path("pop restores outer dotted key", handler, "a.b");
    handler.setKey("after", 5);
    checks.path("root sibling after nested objects", handler, "after");
    handler.pop();
    checks.path("root pop restores empty state", handler, "");

    PathHandler arrays;
    arrays.startArraySpecial();
    checks.path("root array convention", arrays, "array.array");
    arrays.startArraySpecial();
    checks.path("nested root array repeats key", arrays, "array.array.array");
    arrays.pop();
    checks.path("nested array restores parent", arrays, "array.array");
    arrays.push();
    arrays.setKey("key.with.dot", 12);
    checks.path("object inside root array", arrays, "array.array.key.with.dot");
    arrays.pop();
    checks.path("object pop restores array scalar path", arrays, "array.array");
    arrays.pop();
    checks.path("root array pop restores root", arrays, "");

    PathHandler mixed;
    mixed.push();
    mixed.setKey("a.b", 3);
    mixed.startArraySpecial();
    checks.path("named array duplicates whole dotted key", mixed, "a.b.a.b");
    mixed.push();
    mixed.setKey("child", 5);
    checks.path("object in named array", mixed, "a.b.a.b.child");
    mixed.pop();
    checks.path("object in array restores whole dotted key", mixed, "a.b.a.b");
    mixed.pop();
    mixed.setKey("sibling", 7);
    checks.path("named array restores containing object", mixed, "sibling");
    mixed.pop();
    checks.path("mixed structure returns to root", mixed, "");
}

void empty_and_binary_keys(Checks& checks) {
    PathHandler empty;
    empty.push();
    empty.setKey(nullptr, 0);
    checks.path("null pointer with zero-length key", empty, "");
    empty.push();
    empty.setKey("", 0);
    checks.path("nested empty keys follow display convention", empty, "");
    empty.setKey("x", 1);
    checks.path("child under empty parent", empty, "x");
    empty.pop();
    checks.path("empty parent restored", empty, "");
    empty.pop();
    checks.path("empty root restored", empty, "");

    PathHandler binary;
    constexpr std::array parent = {'a', '\0', 'b'};
    constexpr std::array child = {'c', '\0', 'd'};
    const std::string parent_string(parent.data(), parent.size());
    const std::string child_string(child.data(), child.size());
    binary.push();
    binary.setKey(parent.data(), parent.size());
    binary.push();
    binary.setKey(child.data(), child.size());
    checks.path("embedded NUL keys preserve bytes", binary, parent_string + "." + child_string);
    binary.push();
    binary.setKey("leaf", 4);
    checks.path("embedded NUL nested prefix", binary, parent_string + "." + child_string + ".leaf");
    binary.pop();
    checks.path("embedded NUL nested parent restored", binary, parent_string + "." + child_string);
    binary.pop();
    checks.path("embedded NUL outer parent restored", binary, parent_string);
    binary.setKey("after", 5);
    checks.path("sibling after embedded NUL container", binary, "after");
    binary.pop();
    checks.path("binary-key structure returns to root", binary, "");
}

void boundaries_and_errors(Checks& checks) {
    const std::size_t limit = JSON_STRING_SIZE;
    const std::string max_key(limit - 1, 'k');
    PathHandler key;
    key.setKey(max_key.data(), max_key.size());
    checks.path("key exactly below byte limit", key, max_key);
    const std::string oversized_key = max_key + "k";
    key.setKey(oversized_key.data(), oversized_key.size());
    checks.check("key at byte limit rejected", key.m_error);
    checks.check("errored key has empty name", key.name().empty());
    key.setKey("safe", 4);
    key.push();
    key.pop();
    checks.check("path error stays sticky", key.m_error && key.name().empty());

    const std::string parent((limit - 2) / 2, 'p');
    const std::string child(limit - 2 - parent.size(), 'c');
    PathHandler exact;
    exact.setKey(parent.data(), parent.size());
    exact.push();
    exact.setKey(child.data(), child.size());
    checks.path("full path exactly below byte limit", exact, parent + "." + child);
    exact.push();
    // Entering this container is legal; a further nonempty leaf cannot fit.
    exact.setKey("x", 1);
    checks.check("leaf above full path limit rejected", exact.name().empty() && exact.m_error);

    PathHandler too_long;
    too_long.setKey(parent.data(), parent.size());
    too_long.push();
    const std::string longer_child = child + "c";
    too_long.setKey(longer_child.data(), longer_child.size());
    checks.check("full path at byte limit rejected", too_long.name().empty() && too_long.m_error);
    std::vector untouched(limit, '!');
    too_long.buildArgName(untouched.data());
    checks.check("errored legacy conversion writes no bytes",
        untouched == std::vector(limit, '!'));

    PathHandler push_limit;
    push_limit.setKey(parent.data(), parent.size());
    push_limit.push();
    push_limit.setKey(longer_child.data(), longer_child.size());
    push_limit.push();
    checks.check("oversized container prefix rejected on push", push_limit.m_error);

    PathHandler null_key;
    null_key.setKey(nullptr, 1);
    checks.check("null nonempty key rejected", null_key.m_error);
    PathHandler null_output;
    null_output.buildArgName(nullptr);
    checks.check("null output destination rejected", null_output.m_error);
    PathHandler underflow;
    underflow.pop();
    checks.check("unpaired pop rejected", underflow.m_error);

    PathHandler many;
    many.push();
    const unsigned int depth = 1000;
    for (unsigned int i = 0; i < depth; ++i) {
        many.setKey("x", 1);
        many.push();
    }
    checks.check("deep structural stack valid", !many.m_error);
    for (unsigned int i = 0; i < depth; ++i) many.pop();
    checks.path("deep stack restores root object key", many, "x");
    many.pop();
    checks.path("deep stack returns to scalar root", many, "");
}
} // namespace

int main() {
    try {
        Checks checks;
        structural_paths(checks);
        empty_and_binary_keys(checks);
        boundaries_and_errors(checks);
        return checks.finish();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: PathHandler test setup: %s\n", error.what());
        return 1;
    }
}
