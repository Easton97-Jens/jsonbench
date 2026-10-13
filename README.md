jsonbench
=========

Welcome to the `jsonbench` documentation.

## Description

The purpose of this tool is to compare the performance of different JSON C/C++ libraries and to show what to expect from a possible alternative JSON processor.

Currently, mod_security2 and libmodsecurity3 use YAJL for JSON processing. Unfortunately, this library is deprecated and no longer maintained.


## Supported JSON Engines

The tool currently supports the following JSON parsers:

* YAJL
* RAPIDJSON
* NLOHMANNJSON
* JSONC
* JANSSON
* CJSON
* JSONCPP
* JSONCONS
* SIMDJSON
* YYJSON
* GLAZE = It is only compatible with C++23 or newer and is therefore not entirely suitable for modsecurity as it is based on C++17.

You can list them via:

```bash
src/jsonbench -h
```

---

## Requirements

To compile the code, you need development packages (`-dev`) of the JSON libraries.

### Install required packages (Debian/Ubuntu)

```bash
sudo apt install \
    libyajl-dev \
    rapidjson-dev \
    nlohmann-json3-dev \
    libjson-c-dev \
    libjansson-dev \
    libcjson-dev \
    libjsoncpp-dev \
    libsimdjson-dev \
    libyyjson-dev \
    libjsoncons-dev \
    libglaze-dev
```

Note:

* The `-dev` packages are required for compiling (headers, pkg-config, etc.)
* Runtime libraries (e.g. `libjsoncpp26`, `libsimdjson25`, etc.) are installed automatically

---

### Package availability note

Some packages depend on your distribution version.

Especially:

* **libglaze-dev** is only available in newer Linux distributions
* Older systems may not provide it

You can check availability with:

```bash
apt list | grep glaze
```

or

```bash
apt search libglaze-dev
```

---

### Header-only libraries

These parsers are header-only:

* RapidJSON (`rapidjson-dev`)
* nlohmann JSON (`nlohmann-json3-dev`)

They do not require linking against a library.

---

## Build

```bash
./autogen.sh
./configure 
make
```
./configure optionally with --with-all-json=yes. This option requires all supported JSON libraries to be available and causes configuration to fail if any are missing.

If you want to disable a parser:

```bash
./configure --with-yajl=no
```

---

## Run

Show help:

```bash
src/jsonbench -h
```

Run with a specific parser:

```bash
src/jsonbench -e YAJL
```

Example:

```bash
src/jsonbench -e SIMDJSON -s tests/test01.json
```

---

## Command line options

* `-h` Show help
* `-e` Select JSON engine
* `-d` Max depth (default: 500)
* `-a` Max arguments (default: 500)
* `-n` Number of timed iterations (default: 1); reports mean ± stddev across all runs
* `-s` Silent mode

---

## Example

Single run:

```bash
src/jsonbench -s -e YAJL -a 10000 tests/test06.json
```

Output:

```
Time: 0.000092567 sec
```

Multiple iterations (reports mean ± stddev):

```bash
src/jsonbench -s -n 20 -e SIMDJSON -a 10000 tests/test06.json
```

Output:

```
Time: mean=0.000091234 sec  stddev=0.000002345 sec  (N=20)
```

Run the test suite:

```bash
tests/run_tests.sh           # all available engines
tests/run_tests.sh SIMDJSON  # single engine
```

The runner requires Python 3 (standard library only). Every selected engine
runs the same regression matrix: explicit argument output, valid/invalid JSON,
UTF-8, numeric ranges, size/limit boundaries, silent mode and repeated passes.
Failures and unsupported-limit skips are reported separately; missing expected
output is a failure. A JSONCONS-only run also compares applicable cases to YAJL
when it is installed. Other engines' known defects remain visible as failures.

The jsoncons adapter uses streaming SAX events, strict JSON syntax and lossless
number events. See [the implementation and compatibility notes](docs/jsoncons-sax.md)
and [validation results](docs/validation-results.md).

To exercise the jsoncons C interface separately after configuring the project
(use the same include flags as your normal build):

```bash
g++ -std=c++17 ${CPPFLAGS:-} tests/jsoncons_api_tests.cpp src/jsonconsparser.cpp -o /tmp/jsoncons_api_tests
/tmp/jsoncons_api_tests
```

For memory-safety validation, add `-fsanitize=address,undefined
-fno-omit-frame-pointer` to that compile command. For repeatable DOM/SAX/YAJL
measurements, preserve a pre-change executable and run:

```bash
python3 tests/benchmark_jsoncons.py --baseline /path/to/jsonbench-before --binary src/jsonbench
```

---

## TODO

* Improve documentation for all parsers
* Add more JSON processors

