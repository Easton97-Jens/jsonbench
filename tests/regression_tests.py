#!/usr/bin/env python3
"""Deterministic CLI regression checks, without an external test framework."""

import difflib
import os
from pathlib import Path
import re
import resource
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from typing import Optional


TESTS_DIR = Path(__file__).resolve().parent
# Remove only the final CLI timing trailer. Removing arbitrary Time: lines or
# blank lines would lose real argument data. Byte comparisons retain NUL/UTF-8.
TIMING_TEXT = rb"\nTime: (?:[0-9]+\.[0-9]+ sec|mean=[0-9]+\.[0-9]+ sec  stddev=[0-9]+\.[0-9]+ sec  \(N=[0-9]+\))\n\n"
TIMING_SUFFIX = re.compile(TIMING_TEXT + rb"\Z")
TIMING_PREFIX = re.compile(rb"\A" + TIMING_TEXT)


@dataclass
class Case:
    name: str
    document: bytes
    expected: bytes
    count: int
    yajl_observed: Optional[bytes] = None
    numeric_difference: bool = False

    def output_for(self, engine):
        # YAJL preserves -0 and exponent '+'. jsoncons' public lossless API
        # normalizes those spellings; other engines still must preserve value.
        # nlohmann's native integer SAX event also normalizes integer -0.
        # This is an explicit expected spelling, not output normalization.
        if self.name == "negative-zero" and engine == "NLOHMANNJSON":
            return self.expected
        if self.numeric_difference and engine != "JSONCONS":
            return self.yajl_observed
        return self.expected


def utf8(text):
    return text.encode("utf-8")


VALID_CASES = [
    Case("empty-object", b"{}", b"", 0),
    Case("empty-array", b"[]", b"", 0),
    Case("empty-containers", b'{"a":{},"b":[],"c":[{},[]]}', b"", 0),
    Case("scalars", b'{"s":"text","t":true,"f":false,"n":null,"i":-42,"d":1.25}',
         b"s: text\nt: true\nf: false\nn: \ni: -42\nd: 1.25\n", 6),
    Case("root-string", b'"hello"', b": hello\n", 1),
    Case("root-integer", b"42\n", b": 42\n", 1),
    Case("root-integer-eof", b"42", b": 42\n", 1),
    Case("root-decimal", b"1.25\n", b": 1.25\n", 1),
    Case("root-decimal-eof", b"1.25", b": 1.25\n", 1),
    Case("root-true", b"true", b": true\n", 1),
    Case("root-false", b"false", b": false\n", 1),
    Case("root-null", b"null", b": \n", 1),
    Case("root-array", b'[1,true,null,"s"]',
         b"array.array: 1\narray.array: true\narray.array: \narray.array: s\n", 4),
    Case("nested-arrays", b"[[0,1],[2,3]]",
         b"array.array.array: 0\narray.array.array: 1\narray.array.array: 2\narray.array.array: 3\n", 4),
    Case("nested-objects", b'{"a":{"b":{"c":1}},"after":2}', b"a.b.c: 1\nafter: 2\n", 2),
    Case("mixed-arrays", b'{"a":[1,[2,3],4],"b":{"c":5},"end":6}',
         b"a.a: 1\na.a.a: 2\na.a.a: 3\na.a: 4\nb.c: 5\nend: 6\n", 6),
    Case("array-objects", b'{"rows":[{"a":1,"b":2},{"a":3}],"after":4}',
         b"rows.rows.a: 1\nrows.rows.b: 2\nrows.rows.a: 3\nafter: 4\n", 4),
    Case("array-parent-key", b'[{"a":1},2,{"b":3},4]',
         b"array.array.a: 1\narray.array: 2\narray.array.b: 3\narray.array: 4\n", 4),
    Case("array-after-empty", b'{"a":[{"x":[]},1,{},2,[],3]}',
         b"a.a: 1\na.a: 2\na.a: 3\n", 3),
    Case("repeated-keys", b'{"k":1,"k":2,"nest":{"k":3,"k":4}}',
         b"k: 1\nk: 2\nnest.k: 3\nnest.k: 4\n", 4),
    Case("empty-keys", b'{"":{"":1},"last":2}', b": 1\nlast: 2\n", 2),
    Case("dotted-keys", b'{"a.b":{"c":1},"d":2}',
         b"a.b.c: 1\nd: 2\n", 2),
    Case("unicode", utf8('{"Grüße":"日本語 😀","escaped":"\\u20ac\\ud83d\\ude00"}'),
         utf8("Grüße: 日本語 😀\nescaped: €😀\n"), 2),
    Case("literal-unicode-escape", b'{"literal":"\\\\udc00"}', b"literal: \\udc00\n", 1),
    Case("surrogate-across-buffer", b" " * 16379 + b'"\\ud83d\\ude00"', utf8(": 😀\n"), 1),
    Case("escaped-backslash-across-buffer", b" " * 16382 + b'"\\\\udc00"', b": \\udc00\n", 1),
    Case("utf8-across-buffer", b" " * 16381 + utf8('"😀"'), utf8(": 😀\n"), 1),
    Case("escaped-strings", b'{"s":"quote:\\" slash:\\/ backslash:\\\\ tab:\\t line:\\n end"}',
         b's: quote:" slash:/ backslash:\\ tab:\t line:\n end\n', 1),
    Case("embedded-nul", b'{"a\\u0000b":"x\\u0000y"}', b"a\0b: x\0y\n", 1),
    Case("timing-like-value", b'{"Time":"0.000000000 sec","blank":"\\n"}',
         b"Time: 0.000000000 sec\nblank: \n\n", 2),
    Case("numeric-token-order",
         b'{"12":"34","word":"1e+20","list":[11,0.25,{"n":1.20,"s":"-1"},null,true]}',
         b"12: 34\nword: 1e+20\nlist.list: 11\nlist.list: 0.25\nlist.list.n: 1.20\nlist.list.s: -1\nlist.list: \nlist.list: true\n", 8),
    Case("integer-boundaries",
         b'{"min":-9223372036854775808,"max":9223372036854775807,"u":18446744073709551615,"beyond":18446744073709551616,"negative":-9223372036854775809}',
         b"min: -9223372036854775808\nmax: 9223372036854775807\nu: 18446744073709551615\nbeyond: 18446744073709551616\nnegative: -9223372036854775809\n", 5),
    Case("negative-zero", b'{"zero":0,"negative":-0,"decimal":-0.0}',
         b"zero: 0\nnegative: 0\ndecimal: -0.0\n", 3,
         b"zero: 0\nnegative: -0\ndecimal: -0.0\n", True),
    Case("decimal-precision",
         b'{"decimal":1.2300,"precise":0.123456789012345678901234567890,"tiny":1e-10000,"huge":1E9999}',
         b"decimal: 1.2300\nprecise: 0.123456789012345678901234567890\ntiny: 1e-10000\nhuge: 1E9999\n", 4),
    Case("exponents", b'{"plus":1e+03,"capital":1E-03,"zeros":1e-0003,"huge":1E+9999}',
         b"plus: 1e03\ncapital: 1E-03\nzeros: 1e-0003\nhuge: 1E9999\n", 4,
         b"plus: 1e+03\ncapital: 1E-03\nzeros: 1e-0003\nhuge: 1E+9999\n", True),
]

INVALID_CASES = [
    ("empty-input", b""), ("whitespace-only", b" \t\r\n"),
    ("truncated-object", b'{"a":1'), ("truncated-array", b"[1,2"),
    ("truncated-string", b'{"s":"text'), ("truncated-literal", b"[tru"),
    ("missing-closing-brace", b'{"a":{"b":1}'), ("missing-closing-bracket", b'{"a":[1,2}'),
    ("missing-colon", b'{"a" 1}'), ("missing-comma", b"[1 2]"), ("wrong-separator", b'{"a"=1}'),
    ("trailing-array-comma", b"[1,]"), ("trailing-object-comma", b'{"a":1,}'),
    ("invalid-escape", b'"\\q"'), ("short-unicode-escape", b'"\\u123"'),
    ("unpaired-high-surrogate", b'"\\ud800"'), ("unpaired-low-surrogate", b'"\\udc00"'),
    ("high-surrogate-with-nonsurrogate", b'"\\ud800\\u0041"'),
    ("high-surrogate-with-high-surrogate", b'"\\ud800\\ud800"'),
    ("low-surrogate-across-buffer", b" " * 16379 + b'"\\udc00"'),
    ("invalid-surrogate-pair-across-buffer", b" " * 16377 + b'"\\ud800\\u0041"'),
    ("unescaped-newline", b'"a\nb"'), ("utf8-continuation", b'"\x80"'),
    ("utf8-overlong", b'"\xc0\xaf"'), ("utf8-truncated", b'"\xe2\x82"'),
    ("utf8-surrogate", b'"\xed\xa0\x80"'), ("utf8-out-of-range", b'"\xf4\x90\x80\x80"'),
    ("utf8-invalid-key", b'{"\xff":1}'), ("extra-document", b"{} []"),
    ("extra-text", b'{"a":1}xyz'), ("extra-nul", b"{}\0"),
    ("number-leading-zero", b"[01]"), ("number-leading-plus", b"[+1]"),
    ("number-missing-integer", b"[.1]"), ("number-missing-fraction", b"[1.]"),
    ("number-missing-exponent", b"[1e]"), ("number-missing-exponent-value", b"[1e+]"),
    ("number-double-sign", b"[--1]"), ("number-nan", b"[NaN]"),
    ("number-infinity", b"[Infinity]"), ("comment", b"{/* comment */}"),
]


def disable_core_dumps():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


class Suite:
    def __init__(self, binary, selected, available, temp):
        self.binary, self.selected, self.available, self.temp = binary, selected, available, Path(temp)
        self.passed = self.failed = self.skipped = self.differences = 0

    def report(self, status, label):
        print(f"{status:<5} {label}", flush=True)
        field = {"PASS": "passed", "FAIL": "failed", "SKIP": "skipped", "DIFF": "differences"}[status]
        setattr(self, field, getattr(self, field) + 1)

    def fixture(self, name, document):
        path = self.temp / f"{name}.json"
        path.write_bytes(document)
        return path

    def run(self, engine, path, options=(), stdout=subprocess.PIPE):
        command = [str(self.binary), "-e", engine, *map(str, options), str(path)]
        try:
            return subprocess.run(command, stdout=stdout, stderr=subprocess.PIPE, timeout=15,
                                  preexec_fn=disable_core_dumps, check=False)
        except subprocess.TimeoutExpired as error:
            return subprocess.CompletedProcess(command, 124, error.stdout or b"", (error.stderr or b"") + b"\nTest timed out\n")

    @staticmethod
    def arguments(result):
        iterations = 1
        for index, arg in enumerate(result.args[:-1]):
            if arg == "-n":
                iterations = int(result.args[index + 1])
        # For -n>1 the CLI prints timing before the final output pass.
        if iterations > 1:
            match = TIMING_PREFIX.search(result.stdout)
            return result.stdout[match.end():] if match else None
        match = TIMING_SUFFIX.search(result.stdout)
        return result.stdout[:match.start()] if match else None

    @staticmethod
    def diagnostic(result, expected=None, actual=None):
        print(f"      exit status: {result.returncode}; command: {' '.join(result.args)}")
        if result.stderr:
            print("      stderr: " + repr(result.stderr[:1200]))
        if expected is not None and actual is not None:
            print(f"      expected {len(expected)} output bytes, got {len(actual)}")
            lines = difflib.unified_diff(expected.decode("utf-8", "backslashreplace").splitlines(True),
                                        actual.decode("utf-8", "backslashreplace").splitlines(True),
                                        fromfile="expected", tofile="actual")
            for index, line in enumerate(lines):
                if index == 12:
                    print("      ... diff truncated ...")
                    break
                print("      " + repr(line.rstrip("\n")[:300]))
        elif result.stdout:
            print("      stdout: " + repr(result.stdout[:1200]))

    def success(self, engine, name, path, expected=None, options=(), silent=False):
        label = f"[{engine}] {name}"
        options = ["-a", "9999", *options, *(["-s"] if silent else [])]
        result = self.run(engine, path, options)
        actual = self.arguments(result)
        if result.returncode != 0 or actual is None:
            self.report("FAIL", label + (" (missing timing/output trailer)" if actual is None else ""))
            self.diagnostic(result)
            return None
        if silent:
            expected = b""
        if expected is not None and actual != expected:
            self.report("FAIL", label + " (argument output mismatch)")
            self.diagnostic(result, expected, actual)
            return None
        self.report("PASS", label)
        return actual

    def rejected(self, engine, name, path, options=(), diagnostic=None, silent=True):
        result = self.run(engine, path, ["-a", "9999", *options, *(["-s"] if silent else [])])
        self.rejected_result(engine, name, result, diagnostic)

    def rejected_result(self, engine, name, result, diagnostic=None):
        # Signals/timeouts are failures, not successful validation. Unsupported
        # warnings alone do not establish that malformed input was rejected.
        errors = [line for line in result.stderr.splitlines() if b"does not support" not in line]
        good = 0 < result.returncode < 124 and bool(b"\n".join(errors).strip())
        if diagnostic is not None:
            good = good and diagnostic.lower() in result.stderr.lower()
        if good:
            self.report("PASS", f"[{engine}] {name}")
        else:
            self.report("FAIL", f"[{engine}] {name} (expected rejection with a diagnostic)")
            self.diagnostic(result)

    def limit_rejected(self, engine, name, path, options, diagnostic=None, silent=True):
        result = self.run(engine, path, ["-a", "9999", *options, *(["-s"] if silent else [])])
        warning = b"does not support max depth limit" if "depth" in name else b"does not support max arg num limit"
        if 0 <= result.returncode < 124 and warning in result.stderr:
            self.report("SKIP", f"[{engine}] {name} (adapter reports unsupported limit; no enforcement claim)")
        else:
            self.rejected_result(engine, name, result, diagnostic)

    def baseline(self):
        fixtures = ["test01.json", "test02.json", "test03.json", "test04.json", "test06.json", "512KB.json"]
        reference = {}
        if "YAJL" in self.available:
            for fixture in fixtures:
                result = self.run("YAJL", TESTS_DIR / fixture, ["-a", "9999"])
                output = self.arguments(result)
                if result.returncode == 0 and output:
                    reference[fixture] = output
                else:
                    self.report("FAIL", f"[YAJL] baseline reference: {fixture}")
                    self.diagnostic(result)
        for engine in self.selected:
            for fixture in fixtures:
                path = TESTS_DIR / fixture
                self.success(engine, f"existing valid parse/silent: {fixture}", path, silent=True)
                if fixture in reference:
                    label = "existing argument output repeatability" if engine == "YAJL" else "existing argument output vs YAJL"
                    self.success(engine, f"{label}: {fixture}", path, reference[fixture])
                else:
                    self.report("SKIP", f"[{engine}] output comparison: {fixture} (YAJL unavailable; compatibility untested)")
            self.rejected(engine, "reject existing invalid fixture: test05.json", TESTS_DIR / "test05.json")
            self.limit_rejected(engine, "existing depth limit (-d 1)", TESTS_DIR / "test06.json", ["-d", "1"])
            self.limit_rejected(engine, "existing argument limit (-a 1)", TESTS_DIR / "test06.json", ["-a", "1"])

    def matrix(self):
        for engine in self.selected:
            for case in VALID_CASES:
                path = self.fixture(case.name, case.document)
                self.success(engine, f"golden arguments ({case.count}): {case.name}", path,
                             case.output_for(engine), ["-a", str(max(1, case.count))])
                self.success(engine, f"silent validation: {case.name}", path,
                             options=["-a", str(max(1, case.count))], silent=True)
                if case.count > 1:
                    self.limit_rejected(engine, f"argument count exceeds {case.count - 1}: {case.name}",
                                        path, ["-a", str(case.count - 1)])
            for name, document in INVALID_CASES:
                path = self.fixture(name, document)
                self.rejected(engine, f"invalid JSON (silent): {name}", path)
                self.rejected(engine, f"invalid JSON (output): {name}", path, silent=False)
            self.boundaries(engine)
            self.large_document(engine)
            self.operational(engine)
        self.compatibility()

    def compatibility(self):
        if "JSONCONS" not in self.selected:
            return
        for case in VALID_CASES:
            if "YAJL" not in self.available:
                self.report("SKIP", f"[JSONCONS/YAJL] {case.name} (YAJL unavailable; compatibility untested)")
                continue
            path = self.fixture(case.name, case.document)
            jsoncons = self.run("JSONCONS", path, ["-a", "9999"])
            yajl = self.run("YAJL", path, ["-a", "9999"])
            jsoncons_output, yajl_output = self.arguments(jsoncons), self.arguments(yajl)
            observed = case.expected if case.yajl_observed is None else case.yajl_observed
            if jsoncons.returncode != 0 or jsoncons_output != case.expected or yajl.returncode != 0 or yajl_output != observed:
                self.report("FAIL", f"[JSONCONS/YAJL] comparison prerequisite: {case.name}")
                self.diagnostic(jsoncons, case.expected, jsoncons_output)
                self.diagnostic(yajl, observed, yajl_output)
            elif jsoncons_output == yajl_output:
                self.report("PASS", f"[JSONCONS/YAJL] byte-exact compatibility: {case.name}")
            else:
                self.report("DIFF", f"[JSONCONS/YAJL] {case.name} (both outputs checked; documented difference, no compatibility claim)")

    def config_value(self, name, fallback):
        config = self.binary.parent.parent / "config.h"
        if not config.is_file():
            config = TESTS_DIR.parent / "config.h"
        if config.is_file():
            found = re.search(r"^#define\s+" + name + r"\s+([0-9]+)", config.read_text(), re.MULTILINE)
            if found:
                return int(found.group(1))
        return fallback

    def boundaries(self, engine):
        depth = 64
        for kind, document, expected in [
                ("arrays", b"[" * depth + b"1" + b"]" * depth,
                 b".".join([b"array"] * (depth + 1)) + b": 1\n"),
                ("objects", b'{"k":' * depth + b"1" + b"}" * depth,
                 b".".join([b"k"] * depth) + b": 1\n")]:
            path = self.fixture("deep-" + kind, document)
            self.success(engine, f"{kind} exactly at depth 64", path, expected, ["-d", str(depth)])
            self.limit_rejected(engine, f"{kind} exceed depth 63", path, ["-d", str(depth - 1)])
        default_depth = self.config_value("LIMIT_DEPTH", 500)
        exact = self.fixture("default-depth", b"[" * default_depth + b"0" + b"]" * default_depth)
        self.success(engine, f"default depth accepts {default_depth} containers", exact,
                     b".".join([b"array"] * (default_depth + 1)) + b": 0\n")
        path = self.fixture("excessive-depth", b"[" * (default_depth + 1) + b"0" + b"]" * (default_depth + 1))
        self.limit_rejected(engine, f"default depth rejects {default_depth + 1} containers", path, [])
        for root in [b"{}", b"[]", b"1\n"]:
            self.success(engine, f"root at depth 1: {root!r}", self.fixture("depth-root", root),
                         b": 1\n" if root.startswith(b"1") else b"", ["-d", "1"])
        for name, document in [("booleans", b"[true,false]"), ("nulls", b"[null,null]")]:
            self.limit_rejected(engine, f"{name} enforce argument limit with output", self.fixture(name, document),
                                ["-a", "1"], silent=False)
        size = self.config_value("JSON_STRING_SIZE", 8192)
        key, value, child = b"k" * (size - 1), b"v" * (size - 1), b"b" * (size - 3)
        length_cases = [
            ("key", b'{"' + key + b'":1}', key + b": 1\n", b'{"' + key + b'k":1}'),
            ("value", b'{"k":"' + value + b'"}', b"k: " + value + b"\n", b'{"k":"' + value + b'v"}'),
            ("combined-path", b'{"a":{"' + child + b'":1}}', b"a." + child + b": 1\n", b'{"a":{"' + child + b'b":1}}')]
        for name, document, expected, oversized in length_cases:
            for silence in (False, True):
                mode = "silent" if silence else "output"
                self.success(engine, f"{name} length {size - 1} ({mode})", self.fixture(name, document), expected, silent=silence)
                self.rejected(engine, f"{name} length {size} rejected ({mode})", self.fixture(name + "-long", oversized), silent=silence)
        giant = b"x" * (2 * 1024 * 1024)
        for name, document in [("huge-string", b'{"k":"' + giant + b'"}'),
                               ("huge-number", b'{"k":1' + b"0" * len(giant) + b"}"),
                               ("huge-unterminated-string", b'{"k":"' + giant)]:
            self.rejected(engine, f"bounded hostile token (silent): {name}", self.fixture(name, document))
            self.rejected(engine, f"bounded hostile token (output): {name}", self.fixture(name, document), silent=False)

    def large_document(self, engine):
        ceiling = self.config_value("FILE_BUFFER_SIZE", 10485760)
        payload = b"x" * 512
        record = b'{"id":123,"pad":"' + payload + b'"}'
        count = max(2000, (ceiling + 1024) // (len(record) + 1) + 1)
        path = self.fixture("large-stream", b"[" + b",".join([record] * count) + b"]")
        expected = (b"array.array.id: 123\narray.array.pad: " + payload + b"\n") * count
        self.success(engine, f"large document ({path.stat().st_size} bytes, {count * 2} arguments)", path,
                     expected, ["-a", str(count * 2)])
        self.success(engine, "large document silent validation", path, options=["-a", str(count * 2)], silent=True)
        self.limit_rejected(engine, "large document argument boundary", path, ["-a", str(count * 2 - 1)])

    def operational(self, engine):
        self.rejected(engine, "missing input file diagnostic", self.temp / "does-not-exist.json")
        # Repeated-key behavior has its own mandatory golden case. Unique keys
        # here isolate state reset/output count across timed iterations.
        path = self.fixture("iterations", b'{"x":1,"y":2}')
        self.success(engine, "three timed iterations print arguments once", path, b"x: 1\ny: 2\n", ["-n", "3", "-a", "2"])
        self.success(engine, "three silent iterations emit no arguments", path, options=["-n", "3", "-a", "2"], silent=True)
        self.limit_rejected(engine, "timed iterations retain argument limit", path, ["-n", "3", "-a", "1"])
        for option in ("-d", "-a"):
            for value in ("-1", "abc", "1x", "1.5", "4294967296", "0"):
                self.rejected(engine, f"CLI rejects {option} {value}", path, [option, value])
        for value in ("-1", "abc", "1x", "1.5", "2147483648", "4294967297", "0"):
            self.rejected(engine, f"CLI rejects -n {value}", path, ["-n", value])
        if Path("/dev/full").exists():
            for iterations in (1, 2):
                with open("/dev/full", "wb") as output:
                    result = self.run(engine, path, ["-n", str(iterations)], stdout=output)
                if 0 < result.returncode < 124 and result.stderr.strip():
                    self.report("PASS", f"[{engine}] output error propagated (-n {iterations})")
                else:
                    self.report("FAIL", f"[{engine}] output error ignored (-n {iterations})")
                    self.diagnostic(result)
        else:
            self.report("SKIP", f"[{engine}] output error test (/dev/full unavailable)")

    def timing(self):
        try:
            runs = int(os.environ.get("JSONBENCH_TIMING_RUNS", "0"))
        except ValueError:
            self.report("FAIL", "JSONBENCH_TIMING_RUNS must be a nonnegative integer")
            return
        if runs < 0:
            self.report("FAIL", "JSONBENCH_TIMING_RUNS must be a nonnegative integer")
        elif runs:
            print(f"\n=== Timing: {runs} iterations per fixture, silent mode ===")
            for fixture in ["test01.json", "test06.json", "512KB.json"]:
                for engine in self.selected:
                    result = self.run(engine, TESTS_DIR / fixture, ["-s", "-a", "9999", "-n", str(runs)])
                    if result.returncode != 0 or self.arguments(result) != b"":
                        self.report("FAIL", f"[{engine}] timing: {fixture}")
                        self.diagnostic(result)
                    else:
                        print(f"{engine:14} {fixture:12} {result.stdout.decode().strip()}")


def main():
    if len(sys.argv) not in (2, 3):
        print("Usage: regression_tests.py BINARY [JSON_ENGINE]", file=sys.stderr)
        return 1
    binary = Path(sys.argv[1]).resolve()
    try:
        result = subprocess.run([str(binary), "-h"], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=15, preexec_fn=disable_core_dumps, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: cannot discover engines: {error}", file=sys.stderr)
        return 1
    available = re.findall(r"^\s*-\s+([A-Z]+)\s*$", result.stdout.decode(), re.MULTILINE)
    if result.returncode != 0 or not available:
        print("ERROR: cannot discover engines from binary -h", file=sys.stderr)
        return 1
    selected = available
    if len(sys.argv) == 3:
        if sys.argv[2] not in available:
            print(f"ERROR: engine '{sys.argv[2]}' unavailable (available: {' '.join(available)})", file=sys.stderr)
            return 1
        selected = [sys.argv[2]]
    print(f"=== Available engines: {' '.join(available)} ===")
    print(f"=== Selected engines: {' '.join(selected)} ===")
    with tempfile.TemporaryDirectory(prefix="jsonbench-") as temp:
        suite = Suite(binary, selected, available, temp)
        suite.baseline()
        suite.matrix()
        suite.timing()
        print(f"\n=== Results: {suite.passed} passed, {suite.failed} failed, {suite.skipped} skipped, "
              f"{suite.differences} checked behavioral differences ===")
        return 1 if suite.failed else 0


if __name__ == "__main__":
    sys.exit(main())
