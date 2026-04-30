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

static char available_engines[16][20] = {""};
static int engine_count = 0;

static void showhelp(void) {
    printf("Use: jsonbench [OPTIONS]\n\n");
    printf("OPTIONS:\n");
    printf("\t-h\tThis help\n");
    printf("\t-d\tSet maximum depth of JSON structure, default: %d\n", LIMIT_DEPTH);
    printf("\t-a\tSet maximum number of possible ARGS, default: %d\n", LIMIT_ARG_NUM);
    printf("\t-n\tNumber of timed iterations, default: 1\n");
    printf("\t-s\tBe silence; don't print out the parsed data\n");
    if (engine_count > 0) {
        printf("\t-e\tUse JSON engine\n");
        printf("\t  \tavailable engines:\n");
        for(int i = 0; i < engine_count; i++) {
            printf("\t  \t- %s\n", available_engines[i]);
        }
    }
    else {
        printf("No JSON engine available\n");
    }
    printf("\n");
}

int main(int argc, char ** argv) {
    char           c;
    char          *jsonengine = NULL;

    extern char   *optarg;
    extern int     optind, opterr, optopt;

    char          *error_msg = NULL;
    const char    *jsonfile = NULL;
    unsigned int   depth_limit = LIMIT_DEPTH;
    unsigned int   arg_limit   = LIMIT_ARG_NUM;
    int            silence = 0;
    int            n = 1;
    struct timespec ts_before, ts_after, ts_diff;

#if HAVE_YAJL
    strcpy(available_engines[engine_count++], "YAJL");
#endif
#if HAVE_RAPIDJSON
    strcpy(available_engines[engine_count++], "RAPIDJSON");
#endif
#if HAVE_NLOHMANNJSON
    strcpy(available_engines[engine_count++], "NLOHMANNJSON");
#endif
#if HAVE_JSONC
    strcpy(available_engines[engine_count++], "JSONC");
#endif
#if HAVE_JANSSON
    strcpy(available_engines[engine_count++], "JANSSON");
#endif
#if HAVE_CJSON
    strcpy(available_engines[engine_count++], "CJSON");
#endif
#if HAVE_JSONCPP
    strcpy(available_engines[engine_count++], "JSONCPP");
#endif
#if HAVE_JSONCONS
    strcpy(available_engines[engine_count++], "JSONCONS");
#endif
#if HAVE_SIMDJSON
    strcpy(available_engines[engine_count++], "SIMDJSON");
#endif
#if HAVE_YYJSON
    strcpy(available_engines[engine_count++], "YYJSON");
#endif
#if HAVE_GLAZE
    strcpy(available_engines[engine_count++], "GLAZE");
#endif

    while ((c = getopt(argc, argv, "he:a:d:n:s")) != -1) {
        switch (c) {
            case 'h':
                showhelp();
                return 0;
            case 'e':
                jsonengine = strdup(optarg);
                if (jsonengine == NULL) {
                    fprintf(stderr, "Memory allocation error\n");
                    return EXIT_FAILURE;
                }

                int i = engine_count;
                for(int e = 0; e < engine_count; e++) {
                    if (strcmp(jsonengine, available_engines[e]) == 0) {
                        i = e;
                        break;
                    }
                }

                if (i == engine_count) {
                    fprintf(stderr, "JSON engine '%s' is not available!\n", jsonengine);
                    free(jsonengine);
                    return EXIT_FAILURE;
                }
                break;
            case 'a':
                arg_limit = atoi(optarg);
                if (arg_limit == 0 || arg_limit > (unsigned int)UINT_MAX) {
                    fprintf(stderr, "Ohh... Try to pass for '-a' an integer between 0 and %u\n",
                            (unsigned int)UINT_MAX);
                    free(jsonengine);
                    return EXIT_FAILURE;
                }
                break;
            case 'd':
                depth_limit = atoi(optarg);
                if (depth_limit == 0 || depth_limit > (unsigned int)UINT_MAX) {
                    fprintf(stderr, "Ohh... Try to pass for '-d' an integer between 0 and %u\n",
                            (unsigned int)UINT_MAX);
                    free(jsonengine);
                    return EXIT_FAILURE;
                }
                break;
            case 'n':
                n = atoi(optarg);
                if (n <= 0) {
                    fprintf(stderr, "'-n' must be a positive integer\n");
                    free(jsonengine);
                    return EXIT_FAILURE;
                }
                break;
            case 's':
                silence = 1;
                break;
            default:
                showhelp();
                free(jsonengine);
                return 0;
        }
    }

    if (jsonengine == NULL) {
        printf("No JSON engine was given!\n");
        return 0;
    }

    for (int i = optind; i < argc; i++) {
        if (jsonfile == NULL) {
            jsonfile = argv[i];
        }
    }

    if (jsonfile == NULL) {
        printf("No JSON file was given!\n");
        free(jsonengine);
        return EXIT_FAILURE;
    }

    /* When running multiple iterations, silence output during timed passes to
     * avoid measuring printf overhead. A final non-timed pass prints output
     * if the user did not request silence. */
    int timed_silence = (n > 1) ? 1 : silence;
    int rc = 0;

    double *times = malloc((size_t)n * sizeof(double));
    if (times == NULL) {
        fprintf(stderr, "Memory allocation error\n");
        free(jsonengine);
        return EXIT_FAILURE;
    }

#if HAVE_YAJL
    if (strcmp(jsonengine, "YAJL") == 0) {
        for (int iter = 0; iter < n; iter++) {
            yajl_json_data *json = NULL;

            clock_gettime(CLOCK_MONOTONIC, &ts_before);

            yajl_json_init(&json, &error_msg);
            if (json == NULL) {
                fprintf(stderr, "Failed to initialize JSON parser\n");
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            json->depth_limit   = depth_limit;
            json->arg_num_limit = arg_limit;
            json->silence       = timed_silence;

            rc = yajl_parse_file(json, jsonfile, &error_msg);
            yajl_json_cleanup(json);

            clock_gettime(CLOCK_MONOTONIC, &ts_after);

            if (rc != 0) {
                if (error_msg) {
                    fprintf(stderr, "Error: %s\n", error_msg);
                    free(error_msg);
                    error_msg = NULL;
                }
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }

            timespec_diff(&ts_after, &ts_before, &ts_diff);
            times[iter] = ts_diff.tv_sec + ts_diff.tv_nsec * 1e-9;
        }

        print_stats(times, n);

        if (n > 1 && !silence) {
            yajl_json_data *json = NULL;
            yajl_json_init(&json, &error_msg);
            if (json != NULL) {
                json->depth_limit   = depth_limit;
                json->arg_num_limit = arg_limit;
                json->silence       = 0;
                yajl_parse_file(json, jsonfile, &error_msg);
                yajl_json_cleanup(json);
            }
        }
    }
#endif

#if HAVE_RAPIDJSON
    if (strcmp(jsonengine, "RAPIDJSON") == 0) {
        for (int iter = 0; iter < n; iter++) {
            rj_parser *json = NULL;

            clock_gettime(CLOCK_MONOTONIC, &ts_before);

            rj_json_init(&json, &error_msg);
            if (json == NULL) {
                fprintf(stderr, "Failed to initialize JSON parser\n");
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            rj_set_max_depth(json, depth_limit);
            rj_set_max_arg_num(json, arg_limit);
            rj_set_silence(json, timed_silence);

            rc = rj_parse_file(json, jsonfile, &error_msg);
            rj_json_cleanup(json);

            clock_gettime(CLOCK_MONOTONIC, &ts_after);

            if (rc != 0) {
                if (error_msg) {
                    fprintf(stderr, "Error: %s\n", error_msg);
                    free(error_msg);
                    error_msg = NULL;
                }
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }

            timespec_diff(&ts_after, &ts_before, &ts_diff);
            times[iter] = ts_diff.tv_sec + ts_diff.tv_nsec * 1e-9;
        }

        print_stats(times, n);

        if (n > 1 && !silence) {
            rj_parser *json = NULL;
            rj_json_init(&json, &error_msg);
            if (json != NULL) {
                rj_set_max_depth(json, depth_limit);
                rj_set_max_arg_num(json, arg_limit);
                rj_set_silence(json, 0);
                rj_parse_file(json, jsonfile, &error_msg);
                rj_json_cleanup(json);
            }
        }
    }
#endif

#if HAVE_NLOHMANNJSON
    if (strcmp(jsonengine, "NLOHMANNJSON") == 0) {
        for (int iter = 0; iter < n; iter++) {
            nl_parser *json = NULL;

            clock_gettime(CLOCK_MONOTONIC, &ts_before);

            nl_json_init(&json, &error_msg);
            if (json == NULL) {
                fprintf(stderr, "Failed to initialize JSON parser\n");
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            nl_set_max_depth(json, depth_limit);
            nl_set_max_arg_num(json, arg_limit);
            nl_set_silence(json, timed_silence);

            rc = nl_parse_file(json, jsonfile, &error_msg);
            nl_json_cleanup(json);

            clock_gettime(CLOCK_MONOTONIC, &ts_after);

            if (rc != 0) {
                if (error_msg) {
                    fprintf(stderr, "Error: %s\n", error_msg);
                    free(error_msg);
                    error_msg = NULL;
                }
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }

            timespec_diff(&ts_after, &ts_before, &ts_diff);
            times[iter] = ts_diff.tv_sec + ts_diff.tv_nsec * 1e-9;
        }

        print_stats(times, n);

        if (n > 1 && !silence) {
            nl_parser *json = NULL;
            nl_json_init(&json, &error_msg);
            if (json != NULL) {
                nl_set_max_depth(json, depth_limit);
                nl_set_max_arg_num(json, arg_limit);
                nl_set_silence(json, 0);
                nl_parse_file(json, jsonfile, &error_msg);
                nl_json_cleanup(json);
            }
        }
    }
#endif

#if HAVE_JSONC
    if (strcmp(jsonengine, "JSONC") == 0) {
        for (int iter = 0; iter < n; iter++) {
            jsonc_parser *json = NULL;

            clock_gettime(CLOCK_MONOTONIC, &ts_before);

            rc = jsonc_json_init(&json, &error_msg);
            if (rc != 0 || json == NULL) {
                fprintf(stderr, "Failed to initialize JSON parser\n");
                if (error_msg) { fprintf(stderr, "Error: %s\n", error_msg); free(error_msg); error_msg = NULL; }
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            rc = jsonc_set_max_depth(json, depth_limit);
            if (rc == 1) {
                if (iter == 0) fprintf(stderr, "JSONC does not support max depth limit\n");
            } else if (rc != 0) {
                fprintf(stderr, "jsonc_set_max_depth failed with code %d\n", rc);
                jsonc_json_cleanup(json);
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            rc = jsonc_set_max_arg_num(json, arg_limit);
            if (rc == 1) {
                if (iter == 0) fprintf(stderr, "JSONC does not support max arg num limit\n");
            } else if (rc != 0) {
                fprintf(stderr, "jsonc_set_max_arg_num failed with code %d\n", rc);
                jsonc_json_cleanup(json);
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            jsonc_set_silence(json, timed_silence);

            rc = jsonc_parse_file(json, jsonfile, &error_msg);
            jsonc_json_cleanup(json);

            clock_gettime(CLOCK_MONOTONIC, &ts_after);

            if (rc != 0) {
                if (error_msg) { fprintf(stderr, "Error: %s\n", error_msg); free(error_msg); error_msg = NULL; }
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }

            timespec_diff(&ts_after, &ts_before, &ts_diff);
            times[iter] = ts_diff.tv_sec + ts_diff.tv_nsec * 1e-9;
        }

        print_stats(times, n);

        if (n > 1 && !silence) {
            jsonc_parser *json = NULL;
            jsonc_json_init(&json, &error_msg);
            if (json != NULL) {
                jsonc_set_max_depth(json, depth_limit);
                jsonc_set_max_arg_num(json, arg_limit);
                jsonc_set_silence(json, 0);
                jsonc_parse_file(json, jsonfile, &error_msg);
                jsonc_json_cleanup(json);
            }
        }
    }
#endif

#if HAVE_JANSSON
    if (strcmp(jsonengine, "JANSSON") == 0) {
        for (int iter = 0; iter < n; iter++) {
            jansson_parser *json = NULL;

            clock_gettime(CLOCK_MONOTONIC, &ts_before);

            rc = jansson_json_init(&json, &error_msg);
            if (rc != 0 || json == NULL) {
                fprintf(stderr, "Failed to initialize JSON parser\n");
                if (error_msg) { fprintf(stderr, "Error: %s\n", error_msg); free(error_msg); error_msg = NULL; }
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            rc = jansson_set_max_depth(json, depth_limit);
            if (rc == 1) {
                if (iter == 0) fprintf(stderr, "JANSSON does not support max depth limit\n");
            } else if (rc != 0) {
                fprintf(stderr, "jansson_set_max_depth failed with code %d\n", rc);
                jansson_json_cleanup(json);
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            rc = jansson_set_max_arg_num(json, arg_limit);
            if (rc == 1) {
                if (iter == 0) fprintf(stderr, "JANSSON does not support max arg num limit\n");
            } else if (rc != 0) {
                fprintf(stderr, "jansson_set_max_arg_num failed with code %d\n", rc);
                jansson_json_cleanup(json);
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            jansson_set_silence(json, timed_silence);

            rc = jansson_parse_file(json, jsonfile, &error_msg);
            jansson_json_cleanup(json);

            clock_gettime(CLOCK_MONOTONIC, &ts_after);

            if (rc != 0) {
                if (error_msg) { fprintf(stderr, "Error: %s\n", error_msg); free(error_msg); error_msg = NULL; }
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }

            timespec_diff(&ts_after, &ts_before, &ts_diff);
            times[iter] = ts_diff.tv_sec + ts_diff.tv_nsec * 1e-9;
        }

        print_stats(times, n);

        if (n > 1 && !silence) {
            jansson_parser *json = NULL;
            jansson_json_init(&json, &error_msg);
            if (json != NULL) {
                jansson_set_max_depth(json, depth_limit);
                jansson_set_max_arg_num(json, arg_limit);
                jansson_set_silence(json, 0);
                jansson_parse_file(json, jsonfile, &error_msg);
                jansson_json_cleanup(json);
            }
        }
    }
#endif

#if HAVE_CJSON
    if (strcmp(jsonengine, "CJSON") == 0) {
        for (int iter = 0; iter < n; iter++) {
            cjson_parser *json = NULL;

            clock_gettime(CLOCK_MONOTONIC, &ts_before);

            rc = cjson_json_init(&json, &error_msg);
            if (rc != 0 || json == NULL) {
                fprintf(stderr, "Failed to initialize JSON parser\n");
                if (error_msg) { fprintf(stderr, "Error: %s\n", error_msg); free(error_msg); error_msg = NULL; }
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            rc = cjson_set_max_depth(json, depth_limit);
            if (rc == 1) {
                if (iter == 0) fprintf(stderr, "CJSON does not support max depth limit\n");
            } else if (rc != 0) {
                fprintf(stderr, "cjson_set_max_depth failed with code %d\n", rc);
                cjson_json_cleanup(json);
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            rc = cjson_set_max_arg_num(json, arg_limit);
            if (rc == 1) {
                if (iter == 0) fprintf(stderr, "CJSON does not support max arg num limit\n");
            } else if (rc != 0) {
                fprintf(stderr, "cjson_set_max_arg_num failed with code %d\n", rc);
                cjson_json_cleanup(json);
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            cjson_set_silence(json, timed_silence);

            rc = cjson_parse_file(json, jsonfile, &error_msg);
            cjson_json_cleanup(json);

            clock_gettime(CLOCK_MONOTONIC, &ts_after);

            if (rc != 0) {
                if (error_msg) { fprintf(stderr, "Error: %s\n", error_msg); free(error_msg); error_msg = NULL; }
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }

            timespec_diff(&ts_after, &ts_before, &ts_diff);
            times[iter] = ts_diff.tv_sec + ts_diff.tv_nsec * 1e-9;
        }

        print_stats(times, n);

        if (n > 1 && !silence) {
            cjson_parser *json = NULL;
            cjson_json_init(&json, &error_msg);
            if (json != NULL) {
                cjson_set_max_depth(json, depth_limit);
                cjson_set_max_arg_num(json, arg_limit);
                cjson_set_silence(json, 0);
                cjson_parse_file(json, jsonfile, &error_msg);
                cjson_json_cleanup(json);
            }
        }
    }
#endif

#if HAVE_JSONCPP
    if (strcmp(jsonengine, "JSONCPP") == 0) {
        for (int iter = 0; iter < n; iter++) {
            jsoncpp_parser *json = NULL;

            clock_gettime(CLOCK_MONOTONIC, &ts_before);

            rc = jsoncpp_json_init(&json, &error_msg);
            if (rc != 0 || json == NULL) {
                fprintf(stderr, "Failed to initialize JSON parser\n");
                if (error_msg) { fprintf(stderr, "Error: %s\n", error_msg); free(error_msg); error_msg = NULL; }
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            rc = jsoncpp_set_max_depth(json, depth_limit);
            if (rc == 1) {
                if (iter == 0) fprintf(stderr, "JSONCPP does not support max depth limit\n");
            } else if (rc != 0) {
                fprintf(stderr, "jsoncpp_set_max_depth failed with code %d\n", rc);
                jsoncpp_json_cleanup(json);
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            rc = jsoncpp_set_max_arg_num(json, arg_limit);
            if (rc == 1) {
                if (iter == 0) fprintf(stderr, "JSONCPP does not support max arg num limit\n");
            } else if (rc != 0) {
                fprintf(stderr, "jsoncpp_set_max_arg_num failed with code %d\n", rc);
                jsoncpp_json_cleanup(json);
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            jsoncpp_set_silence(json, timed_silence);

            rc = jsoncpp_parse_file(json, jsonfile, &error_msg);
            jsoncpp_json_cleanup(json);

            clock_gettime(CLOCK_MONOTONIC, &ts_after);

            if (rc != 0) {
                if (error_msg) { fprintf(stderr, "Error: %s\n", error_msg); free(error_msg); error_msg = NULL; }
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }

            timespec_diff(&ts_after, &ts_before, &ts_diff);
            times[iter] = ts_diff.tv_sec + ts_diff.tv_nsec * 1e-9;
        }

        print_stats(times, n);

        if (n > 1 && !silence) {
            jsoncpp_parser *json = NULL;
            jsoncpp_json_init(&json, &error_msg);
            if (json != NULL) {
                jsoncpp_set_max_depth(json, depth_limit);
                jsoncpp_set_max_arg_num(json, arg_limit);
                jsoncpp_set_silence(json, 0);
                jsoncpp_parse_file(json, jsonfile, &error_msg);
                jsoncpp_json_cleanup(json);
            }
        }
    }
#endif

#if HAVE_JSONCONS
    if (strcmp(jsonengine, "JSONCONS") == 0) {
        for (int iter = 0; iter < n; iter++) {
            jsoncons_parser *json = NULL;

            clock_gettime(CLOCK_MONOTONIC, &ts_before);

            rc = jsoncons_json_init(&json, &error_msg);
            if (rc != 0 || json == NULL) {
                fprintf(stderr, "Failed to initialize JSON parser\n");
                if (error_msg) { fprintf(stderr, "Error: %s\n", error_msg); free(error_msg); error_msg = NULL; }
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            rc = jsoncons_set_max_depth(json, depth_limit);
            if (rc == 1) {
                if (iter == 0) fprintf(stderr, "JSONCONS does not support max depth limit\n");
            } else if (rc != 0) {
                fprintf(stderr, "jsoncons_set_max_depth failed with code %d\n", rc);
                jsoncons_json_cleanup(json);
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            rc = jsoncons_set_max_arg_num(json, arg_limit);
            if (rc == 1) {
                if (iter == 0) fprintf(stderr, "JSONCONS does not support max arg num limit\n");
            } else if (rc != 0) {
                fprintf(stderr, "jsoncons_set_max_arg_num failed with code %d\n", rc);
                jsoncons_json_cleanup(json);
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            jsoncons_set_silence(json, timed_silence);

            rc = jsoncons_parse_file(json, jsonfile, &error_msg);
            jsoncons_json_cleanup(json);

            clock_gettime(CLOCK_MONOTONIC, &ts_after);

            if (rc != 0) {
                if (error_msg) { fprintf(stderr, "Error: %s\n", error_msg); free(error_msg); error_msg = NULL; }
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }

            timespec_diff(&ts_after, &ts_before, &ts_diff);
            times[iter] = ts_diff.tv_sec + ts_diff.tv_nsec * 1e-9;
        }

        print_stats(times, n);

        if (n > 1 && !silence) {
            jsoncons_parser *json = NULL;
            jsoncons_json_init(&json, &error_msg);
            if (json != NULL) {
                jsoncons_set_max_depth(json, depth_limit);
                jsoncons_set_max_arg_num(json, arg_limit);
                jsoncons_set_silence(json, 0);
                jsoncons_parse_file(json, jsonfile, &error_msg);
                jsoncons_json_cleanup(json);
            }
        }
    }
#endif

#if HAVE_SIMDJSON
    if (strcmp(jsonengine, "SIMDJSON") == 0) {
        for (int iter = 0; iter < n; iter++) {
            simdjson_parser *json = NULL;

            clock_gettime(CLOCK_MONOTONIC, &ts_before);

            rc = simdjson_json_init(&json, &error_msg);
            if (rc != 0 || json == NULL) {
                fprintf(stderr, "Failed to initialize JSON parser\n");
                if (error_msg) { fprintf(stderr, "Error: %s\n", error_msg); free(error_msg); error_msg = NULL; }
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            rc = simdjson_set_max_depth(json, depth_limit);
            if (rc == 1) {
                if (iter == 0) fprintf(stderr, "SIMDJSON does not support max depth limit\n");
            } else if (rc != 0) {
                fprintf(stderr, "simdjson_set_max_depth failed with code %d\n", rc);
                simdjson_json_cleanup(json);
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            rc = simdjson_set_max_arg_num(json, arg_limit);
            if (rc == 1) {
                if (iter == 0) fprintf(stderr, "SIMDJSON does not support max arg num limit\n");
            } else if (rc != 0) {
                fprintf(stderr, "simdjson_set_max_arg_num failed with code %d\n", rc);
                simdjson_json_cleanup(json);
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            simdjson_set_silence(json, timed_silence);

            rc = simdjson_parse_file(json, jsonfile, &error_msg);
            simdjson_json_cleanup(json);

            clock_gettime(CLOCK_MONOTONIC, &ts_after);

            if (rc != 0) {
                if (error_msg) { fprintf(stderr, "Error: %s\n", error_msg); free(error_msg); error_msg = NULL; }
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }

            timespec_diff(&ts_after, &ts_before, &ts_diff);
            times[iter] = ts_diff.tv_sec + ts_diff.tv_nsec * 1e-9;
        }

        print_stats(times, n);

        if (n > 1 && !silence) {
            simdjson_parser *json = NULL;
            simdjson_json_init(&json, &error_msg);
            if (json != NULL) {
                simdjson_set_max_depth(json, depth_limit);
                simdjson_set_max_arg_num(json, arg_limit);
                simdjson_set_silence(json, 0);
                simdjson_parse_file(json, jsonfile, &error_msg);
                simdjson_json_cleanup(json);
            }
        }
    }
#endif

#if HAVE_YYJSON
    if (strcmp(jsonengine, "YYJSON") == 0) {
        for (int iter = 0; iter < n; iter++) {
            yyjson_parser *json = NULL;

            clock_gettime(CLOCK_MONOTONIC, &ts_before);

            rc = yyjson_json_init(&json, &error_msg);
            if (rc != 0 || json == NULL) {
                fprintf(stderr, "Failed to initialize JSON parser\n");
                if (error_msg) { fprintf(stderr, "Error: %s\n", error_msg); free(error_msg); error_msg = NULL; }
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            rc = yyjson_set_max_depth(json, depth_limit);
            if (rc == 1) {
                if (iter == 0) fprintf(stderr, "YYJSON does not support max depth limit\n");
            } else if (rc != 0) {
                fprintf(stderr, "yyjson_set_max_depth failed with code %d\n", rc);
                yyjson_json_cleanup(json);
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            rc = yyjson_set_max_arg_num(json, arg_limit);
            if (rc == 1) {
                if (iter == 0) fprintf(stderr, "YYJSON does not support max arg num limit\n");
            } else if (rc != 0) {
                fprintf(stderr, "yyjson_set_max_arg_num failed with code %d\n", rc);
                yyjson_json_cleanup(json);
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            yyjson_set_silence(json, timed_silence);

            rc = yyjson_parse_file(json, jsonfile, &error_msg);
            yyjson_json_cleanup(json);

            clock_gettime(CLOCK_MONOTONIC, &ts_after);

            if (rc != 0) {
                if (error_msg) { fprintf(stderr, "Error: %s\n", error_msg); free(error_msg); error_msg = NULL; }
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }

            timespec_diff(&ts_after, &ts_before, &ts_diff);
            times[iter] = ts_diff.tv_sec + ts_diff.tv_nsec * 1e-9;
        }

        print_stats(times, n);

        if (n > 1 && !silence) {
            yyjson_parser *json = NULL;
            yyjson_json_init(&json, &error_msg);
            if (json != NULL) {
                yyjson_set_max_depth(json, depth_limit);
                yyjson_set_max_arg_num(json, arg_limit);
                yyjson_set_silence(json, 0);
                yyjson_parse_file(json, jsonfile, &error_msg);
                yyjson_json_cleanup(json);
            }
        }
    }
#endif

#if HAVE_GLAZE
    if (strcmp(jsonengine, "GLAZE") == 0) {
        for (int iter = 0; iter < n; iter++) {
            glaze_parser *json = NULL;

            clock_gettime(CLOCK_MONOTONIC, &ts_before);

            rc = glaze_json_init(&json, &error_msg);
            if (rc != 0 || json == NULL) {
                fprintf(stderr, "Failed to initialize JSON parser\n");
                if (error_msg) { fprintf(stderr, "Error: %s\n", error_msg); free(error_msg); error_msg = NULL; }
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            rc = glaze_set_max_depth(json, depth_limit);
            if (rc == 1) {
                if (iter == 0) fprintf(stderr, "GLAZE does not support max depth limit\n");
            } else if (rc != 0) {
                fprintf(stderr, "glaze_set_max_depth failed with code %d\n", rc);
                glaze_json_cleanup(json);
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            rc = glaze_set_max_arg_num(json, arg_limit);
            if (rc == 1) {
                if (iter == 0) fprintf(stderr, "GLAZE does not support max arg num limit\n");
            } else if (rc != 0) {
                fprintf(stderr, "glaze_set_max_arg_num failed with code %d\n", rc);
                glaze_json_cleanup(json);
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }
            glaze_set_silence(json, timed_silence);

            rc = glaze_parse_file(json, jsonfile, &error_msg);
            glaze_json_cleanup(json);

            clock_gettime(CLOCK_MONOTONIC, &ts_after);

            if (rc != 0) {
                if (error_msg) { fprintf(stderr, "Error: %s\n", error_msg); free(error_msg); error_msg = NULL; }
                free(times);
                free(jsonengine);
                return EXIT_FAILURE;
            }

            timespec_diff(&ts_after, &ts_before, &ts_diff);
            times[iter] = ts_diff.tv_sec + ts_diff.tv_nsec * 1e-9;
        }

        print_stats(times, n);

        if (n > 1 && !silence) {
            glaze_parser *json = NULL;
            glaze_json_init(&json, &error_msg);
            if (json != NULL) {
                glaze_set_max_depth(json, depth_limit);
                glaze_set_max_arg_num(json, arg_limit);
                glaze_set_silence(json, 0);
                glaze_parse_file(json, jsonfile, &error_msg);
                glaze_json_cleanup(json);
            }
        }
    }
#endif

    free(times);
    free(jsonengine);
    return 0;
}
