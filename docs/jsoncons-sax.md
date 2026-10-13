# jsoncons SAX evaluation

This document records the original jsoncons/test PR snapshot. Subsequent parser
changes and current verification are documented in [the follow-up stack](adapter-stack.md).

This change implements the parser evaluation requested in
[ModSecurity issue #3308](https://github.com/owasp-modsecurity/ModSecurity/issues/3308#issuecomment-6142615388).
It does not change ModSecurity or claim integration compatibility with its complete request-body processor.

## Original behavior and implementation

The former adapter read an entire file into an `ostringstream`, constructed a
complete `jsoncons::json` DOM, and discarded it. It produced no arguments and
returned unsupported for depth and argument limits. A single-engine test used
itself as reference; missing output and unsupported limits could therefore
appear successful. The combined runner could terminate during `diff | head`
before reporting all failures.

The new adapter uses public `basic_json_reader`/`json_visitor` APIs (verified with
jsoncons 1.3.2), a streaming binary `ifstream`, and event callbacks. No JSON DOM
or whole-file string is constructed. `read(ec)` includes end-of-input validation
and rejects trailing content. A strict parsing handler rejects comments (the
library's default handler accepts them). Syntax errors include line/column;
I/O, output, allocation and limit failures propagate across the existing C API
without leaking C++ exceptions. Streams and visitor state use RAII.

The expanded tests exposed a jsoncons 1.3.2 decoding defect: a lone escaped low
surrogate is silently dropped, and a high surrogate followed by a non-low code
unit can be converted into the wrong character. A small public streaming-source
wrapper validates Unicode escape pairing across input buffers before forwarding
them. It performs no JSON grammar parsing or whole-file pass. Raw UTF-8 and the
remaining JSON syntax are validated by jsoncons.

Each scalar produces one argument, in event order, including duplicate keys;
null produces an empty value. Empty containers produce none. Limits count root
containers as depth 1 and root scalars as depth 0. A depth/argument limit of N
allows exactly N, then rejects the next container/scalar. Direct C callers can
set zero; CLI `-d`/`-a` require decimal integers in 1..UINT_MAX. Every parse call
starts with fresh state and retains the configured limits. Silent mode suppresses
argument writes while enforcing all parsing, count and size checks.

## Argument names and intentional differences

The customary YAJL names are retained: `{"a":{"b":1}}` produces `a.b: 1`,
`{"a":[1,2]}` produces `a.a: 1` and `a.a: 2`, and `[1,2]` uses `array.array`.
Array indexes are not added. Nested arrays repeat the enclosing array key.
Duplicate object keys remain separate arguments.

A structural stack records prefix lengths and parent keys. The shared
`PathHandler` is unsuitable here: it infers parent boundaries from literal dots,
has an off-by-one output-buffer check, and cannot reliably restore parent keys
containing dots. It and the other engines remain unchanged.

Intentional corrections and numeric normalizations are explicit regression cases:

- `{"a.b":{"c":1},"d":2}` ends with `d: 2`; YAJL incorrectly emits `a.d: 2`.
- Mixed root arrays, including `[{"x":1},2]`, restore the containing array key:
  both adapters produce `array.array.x: 1` then `array.array: 2` in this case.
- Native integers use exact signed/unsigned 64-bit conversions. Oversized
  integers arrive as `bigint` string events, preserving all digits, without
  conversion to floating point or arbitrary-precision arithmetic.
- `lossless_number(true)` delivers decimal/scientific numbers as `bigdec`
  string events. Decimals, exponent magnitude and trailing decimal zeros remain
  exact, including `1e9999` and `1e-9999`. These are argument strings, not claims
  that a downstream binary float can represent their range.
- jsoncons normalizes integer `-0` to `0` and removes an explicit positive
  exponent sign (`1e+03` becomes `1e03`). Negative-zero decimal `-0.0` remains.
  Byte compatibility is only asserted for cases without these differences.
- Decoded embedded NULs are written by length instead of truncated like YAJL's
  `%s` output. Raw newline/colon/dot characters retain the CLI's ambiguous text
  convention; consumers requiring unambiguous framing should use length-aware
  callbacks rather than parse this display format.
- jsoncons finalizes documents and rejects truncated input. YAJL's existing
  file adapter does not call `yajl_complete_parse`, so it can accept truncation.
  Root numbers without a trailing delimiter also lose their argument in YAJL;
  jsoncons emits it correctly. These are two separately tested EOF differences.
- The public jsoncons source adaptor accepts a UTF-8 BOM; YAJL rejects it.
  This was checked separately and is permitted by RFC 8259's parser guidance.

## Memory and safety limits

Keys, full argument paths and decoded values must be shorter than the configured
`JSON_STRING_SIZE` (8192 bytes by default), in both output and silent modes.
Checked dynamic strings avoid fixed-buffer writes. jsoncons buffers an individual
token before visiting it, so its public temporary allocator is additionally
bounded: each token/state allocation may use at most
`4 * JSON_STRING_SIZE + 16384` bytes (48 KiB by default). Excessive strings,
numbers and state growth fail explicitly, including unterminated large tokens.
This budget allows buffer growth and ordinary escaped strings within the value
limit; it is a resource ceiling in addition to semantic limits.

Memory grows with retained path/depth state and bounded tokenizer storage, rather
than with total document size. There is no fixed whole-file size limit in this
adapter. Configured depth above INT_MAX is capped by jsoncons's int-based depth
option; the temporary state allocation ceiling can reject far smaller extreme
depths too. Deep recursion is avoided by the event reader and explicit stack.

The other engines have different syntax, range, output and resource behavior.
The regression runner reports those differences as failures where they violate
the required contract; it does not repair their implementations or label absent
argument output as compatible. Missing optional engines remain optional.

## Verification and integration guidance

Run the repository build and engine matrix documented in the README. The C API
harness independently checks invalid parameters, setter boundaries, zero limits,
cleanup, error ownership and reuse after failed parses. The benchmark helper
compares a preserved pre-change executable against the SAX executable and YAJL;
its deterministic inputs and raw samples make the measurement reproducible.
See `validation-results.md` for measured results and commands.

Before adopting jsoncons in ModSecurity:

1. Pin and test the jsoncons version and strict/lossless options. Test its public
   incremental `json_parser` API with actual body chunks and an explicit final
   parse step; this adapter currently exposes only a file entry point.
2. Send visitor arguments into ModSecurity's length-aware argument API instead
   of stdout. Retain duplicate order, null semantics and both limits.
3. Agree on array names, dotted/empty keys, embedded NULs and numeric lexical
   normalizations; replay a real request corpus with explicit expectations.
4. Match body-size, decoded-token, depth and argument budgets to ModSecurity's
   configuration. Handle partial events on late parse failure consistently with
   request-body error handling; streaming output is not transactional.
5. Measure throughput, allocation, latency and peak memory on representative
   concurrent requests, malformed bodies and production compiler/toolchains.
   SAX parsing here does not establish streaming JSON-schema validation.
