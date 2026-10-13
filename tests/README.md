# Regression tests

Build using the repository's usual `./autogen.sh`, `./configure`, and `make`
commands, then run:

```sh
tests/run_tests.sh           # every available engine, the same required matrix
tests/run_tests.sh JSONCONS  # one engine; still compare with YAJL if available
make check                  # six direct API/path harnesses, optional engines respected
```

The existing shell entry point uses `regression_tests.py`, a Python 3 standard
library helper. It writes deterministic temporary fixtures and deletes them
afterwards. No external test framework is required. All parser subprocesses
have a 15 second timeout and disabled core dumps. A parser crash or timeout is
a failure, rather than successful rejection of invalid JSON.

The matrix applies to **every selected engine**. It checks exact argument names,
byte values, repeated keys, root primitives, nested arrays and objects, empty
containers, booleans/null, Unicode, embedded NUL, escaped characters, numeric
boundaries and precision, malformed JSON/UTF-8, depth and argument boundaries,
long paths/keys/values, huge tokens, a document larger than the legacy file
buffer, silent mode, repeated benchmark iterations, CLI limit validation, file
errors and output errors. Iteration counts reject negative, zero, nonnumeric,
fractional, trailing-junk and overflowing inputs before allocation. Existing
fixtures and cross-engine comparisons remain
included. New expected outputs are explicit data in the helper, not generated
by the parser under test. Argument counts are also tested by running exactly at
the leaf count and one below it. Boolean and null leaves count as arguments;
empty containers do not.

A parse-only adapter that emits no arguments **fails** output tests. An adapter
that explicitly warns that a particular limit is unsupported can skip that
limit check; it still runs parsing, output, numeric and malformed-input tests.
Unavailable YAJL causes a clearly labelled comparison skip. Cross-engine checks
require both engines to be available. YAJL's existing
fixture rechecks are labelled repeatability, rather than compatibility with
itself. Only byte equality after checking the expected outputs is reported as
compatibility. Known differences are separately
reported as `DIFF`, not as compatibility passes. Full-engine runs expose existing
adapter defects and return nonzero when any required assertion fails.

The required numeric contract preserves exact integer precision (including
values outside signed/unsigned 64-bit ranges) and decimal/exponent precision.
jsoncons' public lossless-number API normalizes integer `-0` to `0` and removes
the optional exponent `+`; those two differences have explicit expected results.
Other engines are checked against the original numerical spelling, except that
nlohmann's native integer event also has the explicit -0-to-0 expected result.
The YAJL follow-up fixes path restoration, embedded NUL and root numbers at EOF;
those cases now require exact equality. No output-wide normalization is applied
to comparisons. The helper removes only the CLI timing record at its
defined position, preserving real blank lines and `Time` argument data.

To test another built executable or reproduce baseline failures:

```sh
JSONBENCH_BINARY=/absolute/path/to/jsonbench tests/run_tests.sh JSONCONS
```

Timing is opt-in, so regression runs do not spend time benchmarking every small
fixture. Timing failures also affect the test exit status:

```sh
JSONBENCH_TIMING_RUNS=20 tests/run_tests.sh JSONCONS
```

`jsoncons_api_tests.cpp` separately tests the C-compatible API, including null
parameters, limit validation, zero-limit semantics, cleanup, error ownership,
parser reuse after success/failure, and settings preserved after invalid setters.
After configuring with JSONCONS enabled, compile and run it with the same header
paths used for the main build:

```sh
gcc -std=gnu11 -Wall -Wextra -Werror -c tests/test_c_memory.c \
  -o /tmp/jsonbench-test-c-memory.o
g++ -std=c++17 -Wall -Wextra -Werror $CPPFLAGS \
  tests/jsoncons_api_tests.cpp src/jsonconsparser.cpp \
  /tmp/jsonbench-test-c-memory.o -o /tmp/jsoncons-api-tests
/tmp/jsoncons-api-tests
```

`CPPFLAGS` must include the installed jsoncons include path if it is outside the
compiler's standard paths. Add `-fsanitize=address,undefined
-fno-omit-frame-pointer` to both compilation commands to run the harness with sanitizers.
The API harness returns nonzero on failure and returns 77 if configured without
JSONCONS; that is unavailable coverage, not a passing test.

`make check` builds and runs all six committed harnesses: PathHandler, jsoncons,
YAJL, the RapidJSON/nlohmann SAX pair, the four C DOM adapters, and the
JsonCpp/simdjson pair. They test C API zero limits and rejected setters, caller
diagnostic ownership, reuse after errors, exact output and streaming boundaries.
The C DOM harness also checks decimal-comma process/thread locales when one is
installed; that subcheck reports an explicit skip otherwise. The C++ harness
preserves the simdjson safety ceiling regression: 512 containers accepted with
an enlarged configured limit, 513/999/2000 rejected diagnostically.

These API tests target the configured source tree, independently of a
`JSONBENCH_BINARY` override. Missing engines return 77 and Automake reports
SKIP; enabled compilation or test failures remain failures. CLI required tests
continue to run against every available engine and intentionally fail on
unsupported numeric/duplicate/NUL contracts. See
[`docs/adapter-stack.md`](../docs/adapter-stack.md) for the measured matrix and
remaining native-library limitations.
