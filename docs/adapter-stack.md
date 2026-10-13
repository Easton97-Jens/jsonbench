# Parser follow-up stack

These changes build on the jsoncons SAX/test PR
[#4](https://github.com/digitalwave/jsonbench/pull/4). The earlier
`validation-results.md` records that original snapshot; it is not the result
of these follow-ups. All engine assertions remain enabled.

## 1. YAJL streaming and shared path safety

YAJL now reads bounded 16 KiB chunks, finalizes with `yajl_complete_parse`, and
implements its declared incremental `process`/`complete` API. Root numbers at
physical EOF are emitted; truncated and extra documents fail. Parsing a new file
resets state while retaining settings. C entry points validate pointers and
clean up partial allocations. Containers count toward depth, every scalar
(including boolean/null) counts toward the argument limit, and silent mode
checks the same boundaries.

Checked copies and structural frames replace unchecked key/prefix operations.
Decoded NULs are emitted by length. Narrow raw-UTF-8 and surrogate guards cover
YAJL 2.1's incomplete scalar validation, including overlong encodings, surrogate
codepoints and values above U+10FFFF. A bounded raw-token guard prevents the
lexer from accumulating arbitrarily large individual strings/numbers before
its callbacks. It does not replace native JSON grammar parsing.

The C++ `PathHandler` similarly records structural boundaries instead of
searching for dots. Existing callback method names remain compatible. Keys,
values and full argument names must be shorter than `JSON_STRING_SIZE` (8192
bytes by default). Allocation failures in legacy noexcept path callbacks become
an error flag. The CLI now propagates failed non-timed output passes and final
stdout errors for every engine.

Four old YAJL comparison exceptions are removed: dotted-key restoration,
embedded NUL output, and the two root-number-at-EOF cases must now match their
explicit expected output. Numeric lexical differences for jsoncons (`-0` and
exponent `+`) remain separately checked.

Fresh verification of this step: YAJL CLI matrix **232 passed, 0 failed, 0
skipped**; direct C API **3900 checks passed**; shared path contract **181 checks
passed**. Both direct harnesses also passed ASan/UBSan with leak detection.

```sh
source /workspace/.jsonbench-tools/activate.sh
./autogen.sh && ./configure && make -j2
tests/run_tests.sh YAJL
# Standalone public API checks (use the configured include/link flags):
gcc -std=gnu11 -Wall -Wextra -Werror -c tests/test_c_memory.c \
  -o /tmp/jsonbench-test-c-memory.o
gcc -std=gnu11 -Wall -Wextra -Werror $CPPFLAGS -c src/yajlparser.c -o /tmp/yajl.o
g++ -std=c++17 -Wall -Wextra -Werror $CPPFLAGS \
  tests/yajl_api_tests.cpp /tmp/yajl.o /tmp/jsonbench-test-c-memory.o \
  $LDFLAGS -lyajl -o /tmp/yajl-api-tests
/tmp/yajl-api-tests
g++ -std=c++17 -Wall -Wextra -Werror $CPPFLAGS \
  tests/pathhandler_tests.cpp /tmp/jsonbench-test-c-memory.o -o /tmp/pathhandler-tests
/tmp/pathhandler-tests
```

## 2. Streaming RapidJSON and nlohmann SAX adapters

Both file entry points now stream bounded input instead of loading a fixed-size
whole-file buffer. RapidJSON uses the public FileReadStream and iterative Reader
with encoding validation and raw-number events; nlohmann uses its public SAX
stream API. Each parse gets fresh state, C entry points contain exceptions, and
checked setter values preserve configuration after invalid calls. Boolean/null
callbacks propagate argument-limit failures. Keys/values are length-aware,
including embedded NULs; output and file-read failures propagate.

Narrow raw-NUL, Unicode and token-size guards address the native readers' EOF
sentinels and resource limits. No DOM is constructed. RapidJSON's unsafe Int64/
Uint64 pointer casts are replaced with exact integer text. nlohmann's float
callback uses its provided original token, so delivered decimals and large
integer lexemes are not rounded through to_string(double).

The installed RapidJSON 1.1 and nlohmann 3.11 parsers still reject extremely large
positive floating exponents before callbacks. Those required golden cases remain
failures with explicit range diagnostics. nlohmann's integer event normalizes
-0 to 0; it has an explicit expected spelling, matching the jsoncons integer
semantics. No output-wide numerical normalization is applied.

C API zero limits have the same meaning as jsoncons/YAJL: zero argument limit
allows empty containers but no scalars; zero depth allows only root primitives.
The CLI continues to require positive limits.

Fresh matrices: RapidJSON **228 passed, 4 failed, 0 skipped**; nlohmann **228
passed, 4 failed, 0 skipped**. All four failures per engine are positive exponent
range cases in output/silent mode. The generic required assertions remain intact.
The new `tests/sax_adapter_api_tests.cpp` checks zero limits, invalid setters,
reuse, error ownership and byte-exact output directly across both C interfaces.
It passed **291 checks per engine (582 total)**, including ASan/UBSan with leak
detection. Either engine can be compiled alone; both disabled return skip code
77 rather than a successful test result.

```sh
g++ -std=c++17 -Wall -Wextra -Werror $CPPFLAGS -I. \
  tests/sax_adapter_api_tests.cpp src/rjparser.cpp src/nlparser.cpp \
  /tmp/jsonbench-test-c-memory.o $LDFLAGS -o /tmp/sax-api-tests
/tmp/sax-api-tests
```

## 3. Native DOM and simdjson validation adapters

JSON-C, Jansson, cJSON, JsonCpp and yyjson retain their native DOM APIs. They now
read files dynamically, require complete native grammar validation, check
Unicode/NUL/resource boundaries, enforce depth and scalar counts, retain source
order where possible, and report unsupported native representations explicitly.
They still allocate document-sized input and DOM memory. A narrow shared C
preflight bounds hostile tokens/depth and checks native numeric ranges; it is
not a substitute for native JSON grammar parsing. An iterative shared walker
uses length-aware structural paths. Borrowed raw numeric spans are matched to
native numeric leaves before successful output, avoiding decimal reformatting
without bypassing native range checks. A mixed numeric/string golden fixture
checks that numeric-looking keys and strings do not consume those spans.

JSON-C's strict 0.18 reader rejects raw multibyte characters; after validating
Unicode scalars, this adapter transcodes them to equivalent Unicode escapes
while keeping native strict/UTF-8 flags enabled. JSON-C and Jansson share
incompatible public symbol names: the executable links Jansson first and uses
JSON-C linkhash traversal to avoid ambiguous iterator calls. Their C APIs
cannot retain all required duplicate/NUL-key information, so these cases are
rejected instead of silently producing incorrect arguments. cJSON likewise
rejects decoded NUL and integers outside its lossless double range. JsonCpp
rejects duplicates, uses source offsets for order/raw numbers, and has a native
1023-container ceiling.

simdjson uses public OnDemand traversal, with native numeric validation and raw
numeric output; it does not build a DOM. It does retain padded whole-document
input and a structural index, so it is not a bounded-input SAX stream. Its
recursive adapter has an independent 512-container safety ceiling, including
when the caller increases the configured depth. The default limit of 500 is
unchanged. Committed API tests exercise 500/512 and rejection at 513/999/2000
under an 8 MiB stack and sanitizers; JsonCpp at 999 is also exercised.

JSON-C, Jansson and cJSON isolate native decimal parsing in a temporary POSIX
thread numeric locale and restore the caller locale on success and every error
path. Other locale categories remain intact. If POSIX locale functions are
unavailable, an existing dot-decimal locale still works; a comma-decimal locale
is explicitly unsupported rather than changed globally. Tests cover process
and thread decimal-comma locales, malformed native grammar, preflight failures,
missing files, silence and exact decimal output.

The iteration fixture now uses distinct keys, isolating reset/output behavior.
Duplicate keys remain a separate required golden test for every engine.

## Integrated verification

The standard `./autogen.sh`, `./configure`, `make` build passed with ten engines.
Installed native versions: JSON-C 0.18, Jansson 2.14, cJSON 1.7.18, JsonCpp 1.9.6,
simdjson 3.12.3 and yyjson 0.10.0 (alongside YAJL 2.1, RapidJSON 1.1,
nlohmann 3.11 and jsoncons 1.3.2).
Glaze is unavailable in this environment and was not tested; optional dependency
handling remains intact. The complete CLI run returned **1**, correctly keeping
all native representation limitations visible:

| Engine | Passed | Failed | Remaining required failures |
| --- | ---: | ---: | --- |
| YAJL | 242 | 0 | None |
| RapidJSON | 238 | 4 | Extreme positive floating exponents |
| nlohmann | 238 | 4 | Extreme positive floating exponents |
| JSON-C | 232 | 10 | Duplicate keys, NUL keys, native numeric ranges |
| Jansson | 232 | 10 | Duplicate keys, NUL keys, native numeric ranges |
| cJSON | 234 | 8 | Decoded NUL, native numeric ranges |
| JsonCpp | 240 | 2 | Duplicate keys |
| jsoncons | 242 | 0 | None |
| simdjson | 236 | 6 | Native integer/floating ranges |
| yyjson | 242 | 0 | None |

Each failing valid case is tested in output and silent mode. Unsupported ranges
produce diagnostics, with no silent integer rounding. Assertions are unchanged
for the required contract. There were **2409 passes, 44 failures, 0 skips**, plus
**2 separately checked numeric spelling differences**. This includes 33 exact
jsoncons/YAJL comparisons. `tests/run_tests.sh JSONCONS` returned 0 with **275
passes, 0 failures, 0 skips, 2 differences**; the YAJL run returned 0 with **242
passes**. No crash or timeout was reported.

`make check` runs the six direct API/path harnesses. They contain 181 PathHandler,
220 jsoncons, 3900 YAJL, 582 SAX-pair, 565 C DOM and 331 C++-pair checks. These
harnesses also passed ASan/UBSan with leak detection. An all-engines-disabled
configuration builds successfully and reports PathHandler PASS and five API
SKIPs, with no failures. Source distribution checks include the harnesses,
fixtures and helpers. The CLI runner remains independent and supports selecting
one engine or another executable.

The integrated ASan/UBSan build also passed all six API harnesses and reproduced
the exact CLI counts above. A separate sanitizer log pass checks expected
rejections for hidden reports, as a native rejection and a sanitizer can both
exit nonzero. The sanitizer run used O1, frame pointers, non-PIE executables,
leak detection, and all ten available engines.

## Sonar corrections and retained API advisories

Automatic C-family analysis is configured to report recommendations for C++17,
matching the build, rather than facilities from later standards. No rules,
source files or required regression assertions are excluded. Shared test RAII
owns private fixture directories, descriptors, captured output and diagnostic
strings; a C-compiled helper releases C/POSIX diagnostics with their matching
allocator. The CLI validates iteration counts before allocation and bounds its
timing loop by the actual allocation size. YAJL's private state is grouped inline
without an extra allocation. Native validation and path helpers have simpler
control flow and shared initialization where behavior is identical.

RapidJSON's fresh reader owns one bounded vector scratch arena for token bytes
and parser state. Reallocation preserves bytes and checks the budget and scalar
alignment; the allocator releases its storage when the reader is destroyed.
This follows RapidJSON's allocator lifetime rather than exposing raw ownership
to callers. The public C-compatible parser function types remain unchanged.

Four const recommendations are intentionally retained after the user's explicit
compatibility decision: `rj_parse_buffer`, `rj_parse_file`, `rj_parser_cleanup`
and `nl_parser_cleanup`. Adding `const` to their parser pointer parameters would
change the exact function types and break callers that use existing typed
function pointers, even though direct calls and C symbol names would still work.
These findings are documented, not suppressed or marked accepted in Sonar.

Verification uses the existing GitHub SonarQube Cloud checks for
`Easton97-Jens_jsonbench`. The environment cannot access the direct Sonar API,
so this does not certify a complete project-wide issue inventory. Cloud checks
report 0% coverage because these independently executed tests are not imported
as line coverage; 5,779 passing assertions do not establish a coverage percentage.

## Measured jsoncons/YAJL comparison

The benchmark helper attempts every requested YAJL input size. These measurements
are from the integrated adapter snapshot `e6fe3a3`, before the subsequent Sonar
corrections. With five fresh-process samples after one warmup per variant, that
binary and the preserved original DOM executable produced:

| Input | Original jsoncons DOM (ms / KiB RSS) | jsoncons SAX (ms / KiB RSS) | YAJL streaming (ms / KiB RSS) |
| --- | --- | --- | --- |
| 409564 bytes | 5.099 ± 0.679 / 5344 | 3.294 ± 0.763 / 2766 | 2.061 ± 0.305 / 2642 |
| 5242854 bytes | 66.768 ± 2.053 / 39029 | 42.076 ± 1.298 / 2740 | 24.662 ± 0.880 / 2592 |
| 33554368 bytes | 477.680 ± 44.879 / 228194 | 284.249 ± 18.208 / 2708 | 188.932 ± 22.434 / 2638 |

Times are mean parser time ± sample standard deviation; RSS is mean peak child
RSS. Linux/GCC 14.2 with effective O2; short ASCII strings, integers and booleans
in a root array, silent mode and exact generated argument limit. Binary/config
hashes were checked for changes during measurement. The original DOM adapter
only parses/discards a DOM; SAX also builds paths and enforces limits. These
cached synthetic workloads do not establish ModSecurity throughput. The raw
report is committed as [adapter-stack-benchmark.json](adapter-stack-benchmark.json),
including raw samples, input/binary/config hashes and environment metadata.
Reproduce with:

```sh
python3 tests/benchmark_jsoncons.py --baseline /path/to/original/jsonbench \
  --samples 5 --sizes 400KiB 5MiB 32MiB --output /tmp/benchmark.json
```

For ModSecurity, prioritize the jsoncons SAX adapter for bounded-input,
lossless-number evaluation, then test request-body chunk boundaries, transaction
argument insertion, partial-output rollback after errors, and request-wide
resource limits in an independently licensed integration. YAJL now provides a
safer compatibility reference. RapidJSON/nlohmann remain SAX alternatives with
native exponent limits; simdjson is an OnDemand alternative with whole-input
memory. None of these changes implements JSON Schema evaluation.

Partial SAX arguments can be emitted before a later parse error; an embedding
must discard them for a failed document. DOM adapters require whole-document
memory, so request-wide input limits remain necessary. The multi-library link
order and POSIX locale fallback were checked on Linux; other platforms and
library upgrades need their own build/ABI validation. No separate ModSecurity
integration or fuzzing campaign has been performed here.

## Optional JSON Schema validation

No schema validator dependency is introduced. Schema rules are a separate
concern from the streaming parser's JSON grammar, Unicode and resource checks.
The public APIs of both pboettch/json-schema-validator and Sourcemeta Blaze
validate a DOM, so they are not a drop-in SAX schema evaluator. An additional
schema pass would incur DOM memory and must stay outside parser benchmark
measurements.

The [pboettch validator](https://github.com/pboettch/json-schema-validator)
supports Draft 7 and is MIT-licensed; it uses nlohmann's JSON DOM and does not
support arbitrary numeric ranges. [Blaze](https://github.com/sourcemeta/blaze)
supports newer dialects, including 2019-09 and 2020-12, and uses Sourcemeta's
JSON DOM. Its license is AGPLv3 (or a commercial alternative). jsonbench's
GPLv3 permits an AGPLv3 combination under section 13, with the additional AGPL
conditions. ModSecurity v3 is Apache-2.0 licensed: adding AGPL code is not an
Apache-only distribution choice. Likewise, GPL adapter source from this project
cannot simply be copied into Apache-only ModSecurity; the target adapter's
copyright/license terms must be settled independently.
