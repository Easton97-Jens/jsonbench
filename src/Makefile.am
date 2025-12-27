AM_CXXFLAGS = -Wall -g -O0 -std=c++17 -fPIC
AM_CFLAGS = -Wall -g -O0
AM_LDFLAGS= $(YAJL_LDFLAGS)

bin_PROGRAMS = jsonbench

jsonbench_SOURCES = yajlparser.c rjparser.cpp nlparser.cpp jsonbench.c
jsonbench_CFLAGS = $(AM_CFLAGS) $(YAJL_CFLAGS)
jsonbench_LDADD = $(YAJL_LDADD)
