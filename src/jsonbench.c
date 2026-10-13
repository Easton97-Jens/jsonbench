#include <stdio.h>
#include "../config.h"
#include "yajlparser.h"
#include "rjparser.h"
#include "nlparser.h"
#include "jsoncparser.h"
#include "janssonparser.h"
#include "cjsonparser.h"
#include "jsoncppparser.h"
#include "jsonconsparser.h"
#include "simdjsonparser.h"
#include "yyjsonparser.h"
#include "glazeparser.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <limits.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <stdint.h>

#ifndef LIMIT_ARG_NUM
#define LIMIT_ARG_NUM 500
#endif

#ifndef LIMIT_DEPTH
#define LIMIT_DEPTH 500
#endif

static inline void timespec_diff(const struct timespec *a, const struct timespec *b,
                                  struct timespec *result) {
    result->tv_sec  = a->tv_sec  - b->tv_sec;
    result->tv_nsec = a->tv_nsec - b->tv_nsec;
    if (result->tv_nsec < 0) {
        --result->tv_sec;
        result->tv_nsec += 1000000000L;
    }
}

static void print_stats(const double *times, int n) {
    if (n == 1) {
        printf("\nTime: %.9f sec\n\n", times[0]);
        return;
    }
    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += times[i];
    double mean = sum / n;
    double sumsq = 0.0;
    for (int i = 0; i < n; i++) {
        double d = times[i] - mean;
        sumsq += d * d;
    }
    double stddev = sqrt(sumsq / (n - 1));
    printf("\nTime: mean=%.9f sec  stddev=%.9f sec  (N=%d)\n\n", mean, stddev, n);
}

typedef struct {
    unsigned int depth_limit;
    unsigned int arg_limit;
    int silence;
    int iterations;
    const char *filename;
} benchmark_settings;

typedef struct {
    const char *name;
    int (*parse)(const benchmark_settings *, int, int, char **);
} benchmark_engine;

#if HAVE_YAJL || HAVE_RAPIDJSON || HAVE_NLOHMANNJSON || HAVE_JSONC || HAVE_JANSSON || HAVE_CJSON || HAVE_JSONCPP || HAVE_JSONCONS || HAVE_SIMDJSON || HAVE_YYJSON || HAVE_GLAZE
static int initialization_failed(char **error) {
    if (*error == NULL) *error = strdup("Failed to initialize JSON parser");
    return EXIT_FAILURE;
}

#endif

#if HAVE_RAPIDJSON || HAVE_NLOHMANNJSON || HAVE_JSONC || HAVE_JANSSON || HAVE_CJSON || HAVE_JSONCPP || HAVE_JSONCONS || HAVE_SIMDJSON || HAVE_YYJSON || HAVE_GLAZE
static int check_limit_result(int result, const char *engine, const char *limit,
                              int report_support, char **error) {
    if (result == 0) return 0;
    if (result == 1) {
        if (report_support)
            fprintf(stderr, "%s does not support max %s limit\n", engine, limit);
        return 0;
    }
    if (*error == NULL) *error = strdup("Failed to configure JSON parser limit");
    return EXIT_FAILURE;
}

#endif

/* Keep the native C parser pointer types: casting incompatible init/cleanup
 * function pointers would be undefined behavior. Only the common lifecycle is
 * generated here; all grammar/path work remains in the existing adapters. */
#define DEFINE_ENGINE_RUN(prefix, parser_type, engine_name) \
static int run_##prefix(const benchmark_settings *settings, int silence, \
                        int probe, char **error) { \
    parser_type *parser = NULL; \
    int result = prefix##_json_init(&parser, error); \
    if (result != 0 || parser == NULL) { \
        prefix##_json_cleanup(parser); \
        return initialization_failed(error); \
    } \
    result = check_limit_result(prefix##_set_max_depth(parser, settings->depth_limit), \
                                engine_name, "depth", probe, error); \
    if (result == 0) \
        result = check_limit_result(prefix##_set_max_arg_num(parser, settings->arg_limit), \
                                    engine_name, "arg num", probe, error); \
    if (result == 0) result = prefix##_set_silence(parser, silence); \
    if (result == 0 && !probe) \
        result = prefix##_parse_file(parser, settings->filename, error); \
    prefix##_json_cleanup(parser); \
    return result; \
}

#if HAVE_YAJL
static int run_yajl(const benchmark_settings *settings, int silence,
                    int probe, char **error) {
    yajl_json_data *parser = NULL;
    const int initialized = yajl_json_init(&parser, error);
    if (initialized != 1 || parser == NULL) {
        yajl_json_cleanup(parser);
        return initialization_failed(error);
    }
    parser->depth_limit = settings->depth_limit;
    parser->arg_num_limit = settings->arg_limit;
    parser->silence = silence;
    int result = 0;
    if (!probe) result = yajl_parse_file(parser, settings->filename, error);
    yajl_json_cleanup(parser);
    return result;
}
#endif
#if HAVE_RAPIDJSON
DEFINE_ENGINE_RUN(rj, rj_parser, "RAPIDJSON")
#endif
#if HAVE_NLOHMANNJSON
DEFINE_ENGINE_RUN(nl, nl_parser, "NLOHMANNJSON")
#endif
#if HAVE_JSONC
DEFINE_ENGINE_RUN(jsonc, jsonc_parser, "JSONC")
#endif
#if HAVE_JANSSON
DEFINE_ENGINE_RUN(jansson, jansson_parser, "JANSSON")
#endif
#if HAVE_CJSON
DEFINE_ENGINE_RUN(cjson, cjson_parser, "CJSON")
#endif
#if HAVE_JSONCPP
DEFINE_ENGINE_RUN(jsoncpp, jsoncpp_parser, "JSONCPP")
#endif
#if HAVE_JSONCONS
DEFINE_ENGINE_RUN(jsoncons, jsoncons_parser, "JSONCONS")
#endif
#if HAVE_SIMDJSON
DEFINE_ENGINE_RUN(simdjson, simdjson_parser, "SIMDJSON")
#endif
#if HAVE_YYJSON
DEFINE_ENGINE_RUN(yyjson, yyjson_parser, "YYJSON")
#endif
#if HAVE_GLAZE
DEFINE_ENGINE_RUN(glaze, glaze_parser, "GLAZE")
#endif
#undef DEFINE_ENGINE_RUN

static const benchmark_engine engines[] = {
#if HAVE_YAJL
    {"YAJL", run_yajl},
#endif
#if HAVE_RAPIDJSON
    {"RAPIDJSON", run_rj},
#endif
#if HAVE_NLOHMANNJSON
    {"NLOHMANNJSON", run_nl},
#endif
#if HAVE_JSONC
    {"JSONC", run_jsonc},
#endif
#if HAVE_JANSSON
    {"JANSSON", run_jansson},
#endif
#if HAVE_CJSON
    {"CJSON", run_cjson},
#endif
#if HAVE_JSONCPP
    {"JSONCPP", run_jsoncpp},
#endif
#if HAVE_JSONCONS
    {"JSONCONS", run_jsoncons},
#endif
#if HAVE_SIMDJSON
    {"SIMDJSON", run_simdjson},
#endif
#if HAVE_YYJSON
    {"YYJSON", run_yyjson},
#endif
#if HAVE_GLAZE
    {"GLAZE", run_glaze},
#endif
    {NULL, NULL}
};

/* Do not accept prefixes/negative values or overflow the unsigned parser
 * limits. Parser double parameters represent every UINT_MAX value exactly. */
static int parse_limit(const char *text, unsigned int *value) {
    if (text == NULL || *text == '\0') return 0;
    for (const char *cursor = text; *cursor; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return 0;
    }
    errno = 0;
    char *end = NULL;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (errno == ERANGE || *end != '\0' || parsed == 0 || parsed > UINT_MAX)
        return 0;
    *value = (unsigned int)parsed;
    return 1;
}

static void showhelp(void) {
    printf("Use: jsonbench [OPTIONS]\n\n");
    printf("OPTIONS:\n");
    printf("\t-h\tThis help\n");
    printf("\t-d\tSet maximum depth of JSON structure, default: %d\n", LIMIT_DEPTH);
    printf("\t-a\tSet maximum number of possible ARGS, default: %d\n", LIMIT_ARG_NUM);
    printf("\t-n\tNumber of timed iterations, default: 1\n");
    printf("\t-s\tBe silence; don't print out the parsed data\n");
    if (engines[0].name == NULL) {
        printf("No JSON engine available\n\n");
        return;
    }
    printf("\t-e\tUse JSON engine\n\t  \tavailable engines:\n");
    for (const benchmark_engine *engine = engines; engine->name; ++engine)
        printf("\t  \t- %s\n", engine->name);
    printf("\n");
}

static const benchmark_engine *find_engine(const char *name) {
    for (const benchmark_engine *engine = engines; engine->name; ++engine) {
        if (strcmp(name, engine->name) == 0) return engine;
    }
    return NULL;
}

static int accept_limit_option(int option, const char *argument,
                               benchmark_settings *settings) {
    unsigned int *limit = &settings->arg_limit;
    if (option == 'd') limit = &settings->depth_limit;
    if (parse_limit(argument, limit)) return 0;
    fprintf(stderr, "Pass for '-%c' an integer between 1 and %u\n", option, UINT_MAX);
    return -1;
}

static int accept_option(int option, const char *argument,
                         benchmark_settings *settings,
                         const benchmark_engine **engine) {
    unsigned int iterations = 0;
    switch (option) {
        case 'e':
            *engine = find_engine(argument);
            if (*engine != NULL) return 0;
            fprintf(stderr, "JSON engine '%s' is not available!\n", argument);
            return -1;
        case 'a':
        case 'd':
            return accept_limit_option(option, argument, settings);
        case 'n':
            if (!parse_limit(argument, &iterations) || iterations > INT_MAX) {
                fprintf(stderr, "'-n' must be a positive integer no greater than %d\n", INT_MAX);
                return -1;
            }
            settings->iterations = (int)iterations;
            return 0;
        case 's':
            settings->silence = 1;
            return 0;
        case 'h':
        default:
            showhelp();
            return 1;
    }
}

static int parse_options(int argc, char **argv, benchmark_settings *settings,
                         const benchmark_engine **engine) {
    int option;
    while ((option = getopt(argc, argv, "he:a:d:n:s")) != -1) {
        const int result = accept_option(option, optarg, settings, engine);
        if (result != 0) return result;
    }
    if (*engine == NULL) {
        printf("No JSON engine was given!\n");
        return 1;
    }
    if (optind == argc) {
        printf("No JSON file was given!\n");
        return -1;
    }
    settings->filename = argv[optind];
    return 0;
}

static int report_parser_result(int result, char *error) {
    if (result != 0)
        fprintf(stderr, "Error: %s\n", error ? error : "JSON parser failed");
    free(error);
    return result != 0;
}

static int parse_once(const benchmark_engine *engine,
                      const benchmark_settings *settings, int silence,
                      int probe, double *elapsed) {
    struct timespec before;
    struct timespec after;
    struct timespec difference;
    if (!probe && clock_gettime(CLOCK_MONOTONIC, &before) != 0) {
        fprintf(stderr, "Unable to read benchmark clock\n");
        return EXIT_FAILURE;
    }
    char *error = NULL;
    const int result = engine->parse(settings, silence, probe, &error);
    if (report_parser_result(result, error)) return EXIT_FAILURE;
    if (probe) return 0;
    if (clock_gettime(CLOCK_MONOTONIC, &after) != 0) {
        fprintf(stderr, "Unable to read benchmark clock\n");
        return EXIT_FAILURE;
    }
    timespec_diff(&after, &before, &difference);
    *elapsed = difference.tv_sec + difference.tv_nsec * 1e-9;
    return 0;
}

static int benchmark(const benchmark_engine *engine,
                     const benchmark_settings *settings, double *times,
                     size_t count) {
    if (parse_once(engine, settings, 1, 1, NULL) != 0) return EXIT_FAILURE;
    int timed_silence = settings->silence;
    if (count > 1) timed_silence = 1;
    for (size_t iteration = 0; iteration < count; ++iteration) {
        if (parse_once(engine, settings, timed_silence, 0, &times[iteration]) != 0)
            return EXIT_FAILURE;
    }
    print_stats(times, (int)count);
    if (count > 1 && !settings->silence) {
        double elapsed;
        if (parse_once(engine, settings, 0, 0, &elapsed) != 0) return EXIT_FAILURE;
    }
    if (fflush(stdout) != 0) {
        fprintf(stderr, "Error: Unable to write benchmark output\n");
        return EXIT_FAILURE;
    }
    return 0;
}

int main(int argc, char **argv) {
    benchmark_settings settings = {LIMIT_DEPTH, LIMIT_ARG_NUM, 0, 1, NULL};
    const benchmark_engine *engine = NULL;
    const int parsed = parse_options(argc, argv, &settings, &engine);
    if (parsed < 0) return EXIT_FAILURE;
    if (parsed > 0) return 0;
    const size_t count = (size_t)settings.iterations;
    if (count > SIZE_MAX / sizeof(double)) {
        fprintf(stderr, "Benchmark iteration allocation size overflow\n");
        return EXIT_FAILURE;
    }
    double *times = malloc(count * sizeof(double));
    if (times == NULL) {
        fprintf(stderr, "Memory allocation error\n");
        return EXIT_FAILURE;
    }
    const int result = benchmark(engine, &settings, times, count);
    free(times);
    return result;
}
