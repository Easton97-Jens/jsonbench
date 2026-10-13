# Keep direct API contracts in the project's standard Automake test runner.
# Disabled adapters compile their guarded stubs; their harnesses return 77,
# which Automake reports as SKIP instead of a successful compatibility check.
AM_CPPFLAGS = -I$(top_builddir) -I$(top_srcdir)/src
AM_CFLAGS = -Wall
AM_CXXFLAGS = -Wall -std=c++17

check_PROGRAMS = pathhandler_tests jsoncons_api_tests yajl_api_tests \
                 sax_adapter_api_tests dom_adapter_api_tests cpp_adapter_api_tests
TESTS = $(check_PROGRAMS)
noinst_HEADERS = test_support.h test_c_memory.h

pathhandler_tests_SOURCES = pathhandler_tests.cpp test_c_memory.c

jsoncons_api_tests_SOURCES = jsoncons_api_tests.cpp test_c_memory.c ../src/jsonconsparser.cpp
jsoncons_api_tests_CPPFLAGS = $(AM_CPPFLAGS)

yajl_api_tests_SOURCES = yajl_api_tests.cpp test_c_memory.c ../src/yajlparser.c
yajl_api_tests_CPPFLAGS = $(AM_CPPFLAGS) $(YAJL_CFLAGS)
yajl_api_tests_LDFLAGS = $(YAJL_LDFLAGS)
yajl_api_tests_LDADD = $(YAJL_LDADD)

sax_adapter_api_tests_SOURCES = sax_adapter_api_tests.cpp test_c_memory.c \
                                ../src/rjparser.cpp ../src/nlparser.cpp
sax_adapter_api_tests_CPPFLAGS = $(AM_CPPFLAGS)

dom_adapter_api_tests_SOURCES = dom_adapter_api_tests.c \
                                ../src/jsoncparser.c ../src/janssonparser.c \
                                ../src/cjsonparser.c ../src/yyjsonparser.c
dom_adapter_api_tests_CPPFLAGS = $(AM_CPPFLAGS) $(JSONC_CFLAGS) \
                                $(JANSSON_CFLAGS) $(CJSON_CFLAGS) $(YYJSON_CFLAGS)
dom_adapter_api_tests_LDFLAGS = $(JANSSON_LDFLAGS) $(JSONC_LDFLAGS) \
                              $(CJSON_LDFLAGS) $(YYJSON_LDFLAGS)
# Jansson must precede JSON-C for overlapping unversioned object APIs.
dom_adapter_api_tests_LDADD = $(JANSSON_LDADD) $(JSONC_LDADD) \
                            $(CJSON_LDADD) $(YYJSON_LDADD) -lm

cpp_adapter_api_tests_SOURCES = cpp_adapter_api_tests.cpp test_c_memory.c \
                                ../src/jsoncppparser.cpp ../src/simdjsonparser.cpp
cpp_adapter_api_tests_CPPFLAGS = $(AM_CPPFLAGS) $(JSONCPP_CFLAGS) $(SIMDJSON_CFLAGS)
cpp_adapter_api_tests_LDFLAGS = $(JSONCPP_LDFLAGS) $(SIMDJSON_LDFLAGS)
cpp_adapter_api_tests_LDADD = $(JSONCPP_LDADD) $(SIMDJSON_LDADD)

EXTRA_DIST = README.md run_tests.sh regression_tests.py \
             benchmark_jsoncons.py benchmark_rusage.c \
             test01.json test02.json test03.json test04.json \
             test05.json test06.json 512KB.json
