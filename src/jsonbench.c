#include <stdio.h>
#include "../config.h"
#include "yajlparser.h"
#include "rjparser.h"
#include "nlparser.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <limits.h>
#include <time.h>

#ifndef FILE_BUFFER_SIZE
#define FILE_BUFFER_SIZE 10485760
#endif

#ifndef LIMIT_ARG_NUM
#define LIMIT_ARG_NUM 500
#endif

#ifndef LIMIT_DEPTH
#define LIMIT_DEPTH 500
#endif

// https://gist.github.com/diabloneo/9619917#gistcomment-3364033
static inline void timespec_diff(const struct timespec *a, const struct timespec *b, struct timespec *result) {
    result->tv_sec  = a->tv_sec  - b->tv_sec;
    result->tv_nsec = a->tv_nsec - b->tv_nsec;
    if (result->tv_nsec < 0) {
        --result->tv_sec;
        result->tv_nsec += 1000000000L;
    }
}

static char available_engines[10][20] = {""};
static int engine_count = 0;

static void showhelp(void) {
    printf("Use: jsonbench [OPTIONS]\n\n");
    printf("OPTIONS:\n");
    printf("\t-h\tThis help\n");
    printf("\t-d\tSet maximum depth of JSON structure, default: %d\n", LIMIT_DEPTH);
    printf("\t-a\tSet maximum number of possible ARGS, default: %d\n", LIMIT_ARG_NUM);
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

static int read_file(const char *filename, char *buffer) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        printf("Error: Unable to open file %s\n", filename);
        return -1;
    }

    memset(buffer, 0, FILE_BUFFER_SIZE - 1);
    int i = 0;
    int ci;
    size_t length = 0;
    while ((ci = fgetc(file))) {
        if (ci == EOF || !(i < FILE_BUFFER_SIZE)) {
            break;
        }
        buffer[i++] = ci;
        length++;
    }
    fclose(file);

    if (i == FILE_BUFFER_SIZE && ci != EOF) {
        printf("File too long: %s (max allowed: %d)\n", filename, FILE_BUFFER_SIZE);
        return -1 * EXIT_FAILURE;
    }

    return length;
}

int main(int argc, char ** argv) {
    char           c;
    char          *jsonengine = NULL;

    extern char   *optarg;
    extern int     optind, opterr, optopt;

    char          *error_msg;
    unsigned int   length = 0;
    char           data[FILE_BUFFER_SIZE];  // 100M fixed length
    const char    *jsonfile = NULL;
    unsigned int   depth_limit = LIMIT_DEPTH;
    unsigned int   arg_limit   = LIMIT_ARG_NUM;
    int            silence = 0;
    struct timespec ts_before, ts_after, ts_diff;

#ifdef HAVE_YAJL
strcpy(available_engines[engine_count++], "YAJL");
#endif

#ifdef HAVE_RAPIDJSON
strcpy(available_engines[engine_count++], "RAPIDJSON");
#endif

#ifdef HAVE_NLOHMANNJSON
strcpy(available_engines[engine_count++], "NLOHMANNJSON");
#endif

    while ((c = getopt(argc, argv, "he:a:d:s")) != -1) {
        switch (c) {
            case 'h':
                showhelp();
                return 0;
            case 'e':
                jsonengine    = strdup(optarg);
                if (jsonengine == NULL) {
                    fprintf(stderr, "Memory allocation error\n");
                    return EXIT_FAILURE;
                }
                int i = 0;
                for(int e = 0; e < engine_count; e++) {
                    if (strcmp(jsonengine, available_engines[e]) == 0) {
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
                arg_limit     = atoi(optarg);
                if (arg_limit == 0 || arg_limit > (unsigned int)UINT_MAX) {
                    fprintf(stderr, "Ohh... Try to pass for '-a' an integer between 0 and %u\n", (unsigned int)UINT_MAX);
                    return EXIT_FAILURE;
                }
                break;
            case 'd':
                depth_limit    = atoi(optarg);
                if (depth_limit == 0 || depth_limit > (unsigned int)UINT_MAX) {
                    fprintf(stderr, "Ohh... Try to pass for '-a' an integer between 0 and %u\n", (unsigned int)UINT_MAX);
                    return EXIT_FAILURE;
                }
                break;
            case 's':
                silence = 1;
                break;
            default:
                showhelp();
                return 0;
        }
    }

    if (jsonengine != NULL) {

        for (int i = optind; i < argc; i++) {
            if (jsonfile == NULL) {
                jsonfile = argv[i];
            }
        }

        if (jsonfile == NULL) {
            printf("No JSON file was given!\n");
            return EXIT_FAILURE;
        }

        int rc = read_file(jsonfile, data);
        if (rc == 0) {
            printf("Zero character read from file\n");
            return EXIT_FAILURE;
        }
        else if (rc < 0) {
            printf("Error reading file\n");
            return EXIT_FAILURE;
        }
        else {
            length = rc;
        }
#if HAVE_YAJL
        if (strcmp(jsonengine, "YAJL") == 0) {
            yajl_json_data *json = NULL;
            yajl_json_init(&json, &error_msg);

            if (json != NULL) {

                json->depth_limit   = depth_limit;
                json->arg_num_limit = arg_limit;
                json->silence       = silence;

                clock_gettime(CLOCK_REALTIME, &ts_before);
                if (yajl_json_process_chunk(json, data, length, &error_msg) < 0) {
                    printf("Error: %s\n", error_msg);
                    free(error_msg);
                }
                clock_gettime(CLOCK_REALTIME, &ts_after);
                ts_diff.tv_sec  = 0;
                ts_diff.tv_nsec = 0;
                timespec_diff(&ts_after, &ts_before, &ts_diff);
                yajl_json_cleanup(json);
                printf("\nTime: %ld.%09ld sec\n\n", (long)ts_diff.tv_sec, ts_diff.tv_nsec);
            }
        }
#endif
#if HAVE_RAPIDJSON
        if (strcmp(jsonengine, "RAPIDJSON") == 0) {

            rj_parser *json = NULL;
            rj_json_init(&json, &error_msg);
            if (json == NULL) {
                fprintf(stderr, "Failed to initialize JSON parser\n");
                return 2;
            }

            rj_set_max_depth(json, depth_limit);
            rj_set_max_arg_num(json, arg_limit);
            rj_set_silence(json, silence);

            clock_gettime(CLOCK_REALTIME, &ts_before);
            rc = rj_parse_buffer(json, data, length, &error_msg);
            if (rc != 0) {
                fprintf(stderr, "Parse failed with code %d\n", rc);
                fprintf(stderr, "Error: %s\n", error_msg);
                free(error_msg);
                return 3;
            }
            else {
                clock_gettime(CLOCK_REALTIME, &ts_after);
                ts_diff.tv_sec  = 0;
                ts_diff.tv_nsec = 0;
                timespec_diff(&ts_after, &ts_before, &ts_diff);
                rj_json_cleanup(json);
                printf("\nTime: %ld.%09ld sec\n\n", (long)ts_diff.tv_sec, ts_diff.tv_nsec);
            }

        }
#endif
#if HAVE_NLOHMANNJSON
        if (strcmp(jsonengine, "NLOHMANNJSON") == 0) {

            nl_parser *json = NULL;
            nl_json_init(&json, &error_msg);
            if (json == NULL) {
                fprintf(stderr, "Failed to initialize JSON parser\n");
                return 2;
            }

            nl_set_max_depth(json, depth_limit);
            nl_set_max_arg_num(json, arg_limit);
            nl_set_silence(json, silence);

            clock_gettime(CLOCK_REALTIME, &ts_before);
            rc = nl_parse_buffer(json, data, length, &error_msg);
            if (rc != 0) {
                fprintf(stderr, "Parse failed with code %d\n", rc);
                fprintf(stderr, "Error: %s\n", error_msg);
                free(error_msg);
                return 3;
            }
            else {
                clock_gettime(CLOCK_REALTIME, &ts_after);
                ts_diff.tv_sec  = 0;
                ts_diff.tv_nsec = 0;
                timespec_diff(&ts_after, &ts_before, &ts_diff);
                nl_json_cleanup(json);
                printf("\nTime: %ld.%09ld sec\n\n", (long)ts_diff.tv_sec, ts_diff.tv_nsec);
            }

        }
#endif
        free(jsonengine);
    }
    else {
        printf("No JSON engine was given!\n");
        return 0;
    }

    return 0;
}
