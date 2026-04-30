# JSON Engine Comparison

## Legend

- **SAX**: Event/callback-based parsing.
- **DOM**: Full in-memory JSON representation.
- **Stream / incremental**: Can parse from a stream or in chunks without requiring the complete JSON input as one string.
- **JSON Schema**: Native JSON Schema validation support in the library itself.
- **Header-only**: Can be integrated mainly by adding headers, useful for git submodules and avoiding external link dependencies.
- **Not found**: No native support found in the official documentation/repository checked.

## Parser Libraries

| Library | GitHub | SAX / Event API | DOM API | Stream / Incremental API | JSON Schema Validation | Integration Type | Notes |
|---|---|---:|---:|---:|---:|---|---|
| RapidJSON | https://github.com/Tencent/rapidjson | Yes | Yes | Yes | Yes | Header-only | Supports both SAX and DOM. SAX `Reader` parses from a stream and publishes events to a handler. JSON Schema validation exists and can also work in SAX style while parsing. |
| nlohmann/json | https://github.com/nlohmann/json | Yes | Yes | Limited / input-stream based | Not found natively | Header-only / single-header | Has `json::sax_parse(...)` and a `json_sax` interface. Important: `sax_parse()` returns only `bool` and does not return a JSON value; the user must handle events manually. DOM parsing via `json::parse(...)` is the normal high-level use case. |
| YAJL | https://github.com/lloyd/yajl | Yes | Limited tree interface | Yes | No JSON Schema; validating generator only | Compiled C library | Event-driven SAX-style parser written in ANSI C. Supports stream/incremental parsing and generation. This is the existing external dependency style we may want to avoid. |
| json-c | https://github.com/json-c/json-c | No dedicated SAX found | Yes | Yes / tokener-based chunk parsing | Not found natively | Compiled C library | Provides a reference-counted object model. `json_tokener_parse_ex()` can parse buffers with explicit length and tokener state. |
| Jansson | https://github.com/akheron/jansson | No dedicated SAX found | Yes | Yes / callback input loading | Not found natively | Compiled C library | C library for encoding, decoding, and manipulating JSON. Has `json_load_callback()` to read JSON input repeatedly via callback, but the exposed model is still DOM-like `json_t`. |
| cJSON | https://github.com/DaveGamble/cJSON | No dedicated SAX found | Yes | No dedicated streaming parser found | Not found natively | Single `.c` + `.h` C library | Very small ANSI C parser. Easy to vendor, but not header-only and not SAX/streaming focused. |
| JsonCpp | https://github.com/open-source-parsers/jsoncpp | No dedicated SAX found | Yes | No dedicated streaming parser found | Not found natively | Compiled C++ library | C++ library for manipulating JSON values, including serialization/deserialization. Useful for DOM-style tests/config, less suitable for SAX/streaming requirements. |
| jsoncons | https://github.com/danielaparker/jsoncons | Yes | Yes | Yes / streaming-style APIs | Yes | Header-only | Feature-rich C++ library. Supports JSON-like data formats, DOM-style `basic_json`, streaming/event-style processing, and JSON Schema. |
| simdjson | https://github.com/simdjson/simdjson | No classical SAX API | Yes | Yes / On-Demand and parse-many APIs | Not found natively | Compiled library, also single-header distribution exists | Very high-performance parser. Has DOM and On-Demand APIs; On-Demand is lazy / just-in-time, not classical SAX callbacks. `parse_many` supports streams containing multiple JSON documents. |
| yyjson | https://github.com/ibireme/yyjson | No classical SAX API found | Yes | No dedicated streaming parser found | Not found natively | C library (`.h` + `.c`) | High-performance ANSI C library. Reading returns immutable documents/values; writing uses mutable documents/values. A SAX-like API was requested in an issue, which indicates it is not the normal API model. |
| Glaze | https://github.com/stephenberry/glaze | No classical SAX API found | Object / in-memory oriented | Not primary / not classical SAX streaming | Not found natively | Header-only C++ library | Modern C++ JSON/reflection library. Reads/writes from object memory. Good for typed C++ object serialization, but not primarily a SAX parser. Check project C++ version requirements before adopting. |

## Validator Libraries

| Library | GitHub | SAX / Event API | DOM API | Stream / Incremental API | JSON Schema Validation | Integration Type | Notes |
|---|---|---:|---:|---:|---:|---|---|
| Blaze | https://github.com/sourcemeta/blaze | No | No | Not a parser | Yes | Compiled C++ library | Dedicated high-performance JSON Schema validator. It should be considered a validation component, not a parser replacement. Use it after parsing JSON with another library. |

## Architecture Notes

- Blaze is not a JSON parser.
- If Blaze is used, the architecture should be:

```text
[ JSON Parser ] -> [ JSON Schema Validator ]
