AM_CXXFLAGS = -Wall -g -O0 -std=c++17 -fPIC
AM_CFLAGS = -Wall -g -O0

AM_LDFLAGS = $(YAJL_LDFLAGS) $(JSONC_LDFLAGS) $(JANSSON_LDFLAGS) \
             $(CJSON_LDFLAGS) $(JSONCPP_LDFLAGS) $(SIMDJSON_LDFLAGS) \
             $(YYJSON_LDFLAGS)

bin_PROGRAMS = jsonbench

jsonbench_SOURCES = \
    yajlparser.c \
    rjparser.cpp \
    nlparser.cpp \
    jsoncparser.c \
    janssonparser.c \
    cjsonparser.c \
    jsoncppparser.cpp \
    jsonconsparser.cpp \
    simdjsonparser.cpp \
    yyjsonparser.c \
    glazeparser.cpp \
    jsonbench.c

jsonbench_CPPFLAGS = $(YAJL_CFLAGS) $(JSONC_CFLAGS) $(JANSSON_CFLAGS) \
                     $(CJSON_CFLAGS) $(JSONCPP_CFLAGS) $(SIMDJSON_CFLAGS) \
                     $(YYJSON_CFLAGS) $(GLAZE_CFLAGS)

jsonbench_CFLAGS = $(AM_CFLAGS)
jsonbench_CXXFLAGS = $(AM_CXXFLAGS) $(GLAZE_CXXFLAGS)

jsonbench_LDADD = $(YAJL_LDADD) $(JANSSON_LDADD) $(JSONC_LDADD) \
                  $(CJSON_LDADD) $(JSONCPP_LDADD) $(SIMDJSON_LDADD) \
                  $(YYJSON_LDADD) -lm

# Jansson must precede JSON-C for overlapping unversioned object APIs.
# JSON-C traversal uses linkhash to avoid ambiguous iterator calls.
noinst_HEADERS = cjsonparser.h \
    cpparguments.h \
    domarguments.h \
    glazeparser.h \
    janssonparser.h \
    jsonconsparser.h \
    jsoncparser.h \
    jsoncppparser.h \
    nlparser.h \
    pathhandler.h \
    rjparser.h \
    simdjsonparser.h \
    yajlparser.h \
    yyjsonparser.h
