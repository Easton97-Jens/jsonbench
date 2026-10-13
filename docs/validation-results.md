# Validation results

This document records the original jsoncons/test PR snapshot. Subsequent parser
changes and current verification are documented in [the follow-up stack](adapter-stack.md).

Validated in `/workspace/digitalwave-jsonbench` based on upstream commit
`a89f2e93fb7f1415095d6fb6c1bb5fab6bdc8e6d`. The previous onboarding fork in
`/workspace/jsonbench` was left unchanged. Platform: Debian 13 amd64, GCC 14.2,
jsoncons 1.3.2. Dependencies/tool activation reside outside the checkout under
`/workspace/.jsonbench-tools`; no lockfile or dependency declaration was changed.
Glaze was unavailable and remains optional/unbuilt/unrun. Ten other engines
were discovered and exercised.

## Build and checks

From the target checkout, with the existing tool activation sourced:

```sh
source /workspace/.jsonbench-tools/activate.sh
./autogen.sh
./configure
make -j2
tests/run_tests.sh JSONCONS
tests/run_tests.sh
```

Autotools generation, default configuration and build succeeded. The final
changed parser was compiled with no new warnings. `git diff --check` and shell
syntax checks passed. No ModSecurity source or other parser implementation was
modified. Changes to `jsonbench.c` check unsigned CLI limits and propagate errors
from jsoncons' non-timed output pass with `-n > 1`.

- JSONCONS selection: **260 passed, 0 failed, 0 skipped**; **6 checked behavioral
  differences**, reported separately. Exit 0. 28 explicit cases matched YAJL
  byte for byte. Existing fixtures also matched their YAJL reference output.
- Entire ten-engine matrix: **1563 passed, 629 failed, 156 skipped**, plus the
  same 6 checked differences. Exit 1, as required. All skips are observed
  unsupported-limit warnings; no missing argument output is labelled compatible.
- 56 parser invocations crashed in the full matrix; they were failures, the
  runner completed its summary, and core dumps were disabled. No timeouts.
- Opt-in timing (`JSONBENCH_TIMING_RUNS=2`) also passed for JSONCONS; timing
  errors would fail the run rather than being ignored.
- A preserved pre-change DOM executable fails **107 required assertions** under
  the final JSONCONS matrix (133 passed, 26 unsupported-limit skips, exit 1).
  This confirms the new tests detect the former missing-output/limit behavior.
- A separate source copy configured with `--with-jsoncons=no` built successfully
  and retained nine engines. Selecting JSONCONS there fails clearly as unavailable.

The shared matrix contains 34 valid and 41 invalid explicit fixtures, plus
constructed exact/exceeded depth and argument boundaries, string/path boundaries,
2 MiB hostile tokens, a document above the legacy file-size ceiling, invalid CLI
limits, repeated passes and I/O errors. Fixtures are deterministic temporary
files. Golden expectations compare complete bytes, preserving NULs/newlines and
real `Time` keys. Negative checks require a diagnostic and ordinary failure:
signals/timeouts never count as successful rejection or unsupported skips.

## Results per engine in the full matrix

The separate JSONCONS/YAJL block adds 28 compatibility passes and 6 checked
DIFFs; it is not included in individual rows below.

| Engine | Passed | Failed | Skipped |
|---|---:|---:|---:|
| YAJL | 191 | 41 | 0 |
| RAPIDJSON | 191 | 41 | 0 |
| NLOHMANNJSON | 209 | 23 | 0 |
| JSONC | 93 | 113 | 26 |
| JANSSON | 135 | 71 | 26 |
| CJSON | 122 | 84 | 26 |
| JSONCPP | 106 | 100 | 26 |
| JSONCONS | 232 | 0 | 0 |
| SIMDJSON | 108 | 98 | 26 |
| YYJSON | 148 | 58 | 26 |

The non-jsoncons failures expose existing adapter behavior: numeric reformatting
or loss of range, missing argument output in DOM wrappers, dropped NULs,
incorrect dotted-key restoration, accepted malformed/UTF-8 inputs, missing
finalization, unenforced limits, fixed file buffers and unsafe oversized-key
handling. Individual failures and child exit codes are captured in
`/tmp/jsonbench-root-all-tests.log`. A full replay against the preserved pre-change executable also failed; every
non-jsoncons failure label in the current matrix was present in that baseline.
These counts describe contract checks, not 629 independent bugs. Other engines were retained and their assertions remain
strict; their defects were not patched as part of this jsoncons migration.

## Memory-safety and API evidence

A standalone C API harness compiled with `-Wall -Wextra -Werror` passed **220
checks**. It also passed all 220 under AddressSanitizer + UndefinedBehaviorSanitizer
and leak detection, with no diagnostics:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -g -O1 \
  -fno-omit-frame-pointer -fsanitize=address,undefined $CPPFLAGS \
  tests/jsoncons_api_tests.cpp src/jsonconsparser.cpp -o /tmp/jsoncons-api-tests
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 /tmp/jsoncons-api-tests
```

The complete CLI matrix was additionally run against a separately built
JSONCONS-only ASan/UBSan executable: **226 passed, 0 failed, 40 skipped**, exit 0,
no sanitizer diagnostics. Those 40 skips are YAJL comparisons unavailable in
that isolated build, rather than untested jsoncons functionality. The original
all-engine executable was not replaced for this check. Sanitizer build flags:
`-g -O1 -fno-omit-frame-pointer -fno-pie -fsanitize=address,undefined` and linking
with `-no-pie -fsanitize=address,undefined`.

Independent source review found no remaining material lifetime, allocation,
conversion or buffer issue in the changed implementation. A separate 34-case
Unicode probe verified valid/invalid pairs at 15 offsets around the 16 KiB
buffer boundary, escaped backslashes/quotes, U+10000 and U+10FFFF. Resource
ceilings, partial output on late failures and version-specific surrogate
validation remain explicit integration considerations.

## Performance measurement

```sh
python3 tests/benchmark_jsoncons.py \
  --baseline /workspace/.jsonbench-tools/jsonbench-dom-baseline \
  --binary src/jsonbench --output /tmp/jsonbench-final-benchmark.json
```

One warmup then five fresh-process samples per measured variant; table timing is
CLI parser mean ± sample standard deviation, in milliseconds. Memory is mean
child-process peak RSS in KiB, measured with a compiled Linux `wait4` helper.
Input: root array of identical short three-field objects, silent output, argument
limit set exactly to the generated leaf count. Effective production optimization
was O2. Binary/config/input hashes and raw samples are in the JSON report; hashes
were checked unchanged during the measurement.

| Actual input bytes | Variant | Parser ms, mean ± stddev | Mean peak RSS, KiB |
|---:|---|---:|---:|
| 409,564 | jsoncons_dom | 4.477 ± 0.648 | 5330 |
| 409,564 | jsoncons_sax | 2.932 ± 0.079 | 2671 |
| 409,564 | yajl | 1.498 ± 0.208 | 2734 |
| 5,242,854 | yajl | Unrun: configured file limit | — |
| 5,242,854 | jsoncons_dom | 69.876 ± 8.417 | 38976 |
| 5,242,854 | jsoncons_sax | 41.717 ± 5.489 | 2673 |
| 33,554,368 | yajl | Unrun: configured file limit | — |
| 33,554,368 | jsoncons_dom | 363.273 ± 21.097 | 228209 |
| 33,554,368 | jsoncons_sax | 240.755 ± 22.905 | 2649 |

On this synthetic cached-file workload SAX used substantially less peak memory
than the previous DOM implementation and lower parser time. At 32 MiB the
measured values were approximately 363 ms/223 MiB for DOM and 241 ms/2.6 MiB for
SAX. YAJL was faster on the common smaller fixture; its configured 512 KiB file
buffer prevented larger comparisons. The old DOM adapter merely parsed/discarded
its tree, while SAX also performs argument-path/count/size checks and the Unicode
escape guard. No general ModSecurity throughput or concurrency claim follows.

## Changed and added files

Modified: `src/jsonconsparser.cpp`, `src/jsonconsparser.h`, `src/jsonbench.c`,
`tests/run_tests.sh`, `README.md`.

Added: `tests/regression_tests.py`, `tests/jsoncons_api_tests.cpp`,
`tests/benchmark_jsoncons.py`, `tests/benchmark_rusage.c`, `tests/README.md`,
`docs/jsoncons-sax.md`, `docs/validation-results.md`.

Generated local Autotools artifacts (`build-aux/`, `config.h`, configure backups,
and ignored object/build files) are not source changes and should not be committed.
See [the SAX notes](jsoncons-sax.md) for the six explicit YAJL differences,
optional BOM behavior, remaining resource/API limits and ModSecurity integration
recommendations. This remains a file-parser evaluation, not an implemented
ModSecurity chunked-body or schema-validation integration.
