AM_CXXFLAGS = -Wall -g -O0 -std=c++17 -fPIC
AM_CFLAGS = -Wall -g -O0
AM_LDFLAGS= $(YAJL_LDFLAGS) $(JSONC_LDFLAGS) $(JANSSON_LDFLAGS) $(CJSON_LDFLAGS) $(JSONCPP_LDFLAGS)

bin_PROGRAMS = jsonbench

jsonbench_SOURCES = yajlparser.c rjparser.cpp nlparser.cpp jsoncparser.c janssonparser.c cjsonparser.c jsoncppparser.cpp jsonconsparser.cpp jsonbench.c
jsonbench_CPPFLAGS = $(YAJL_CFLAGS) $(JSONC_CFLAGS) $(JANSSON_CFLAGS) $(CJSON_CFLAGS) $(JSONCPP_CFLAGS)
jsonbench_CFLAGS = $(AM_CFLAGS)
jsonbench_CXXFLAGS = $(AM_CXXFLAGS)
jsonbench_LDADD = $(YAJL_LDADD) $(JSONC_LDADD) $(JANSSON_LDADD) $(CJSON_LDADD) $(JSONCPP_LDADD)
