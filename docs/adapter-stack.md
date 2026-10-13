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
gcc -std=gnu11 -Wall -Wextra -Werror $CPPFLAGS -c src/yajlparser.c -o /tmp/yajl.o
g++ -std=c++17 -Wall -Wextra -Werror $CPPFLAGS \
  tests/yajl_api_tests.cpp /tmp/yajl.o $LDFLAGS -lyajl -o /tmp/yajl-api-tests
/tmp/yajl-api-tests
g++ -std=c++17 -Wall -Wextra -Werror $CPPFLAGS \
  tests/pathhandler_tests.cpp -o /tmp/pathhandler-tests
/tmp/pathhandler-tests
```

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
