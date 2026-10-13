#include "../config.h"
#include <assert.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/jsoncparser.h"
#include "../src/janssonparser.h"
#include "../src/cjsonparser.h"
#include "../src/yyjsonparser.h"
static unsigned checks;
/* mkdtemp atomically creates this private directory with mode 0700. */
static char directory[] = ".jsonbench-dom-api-XXXXXX";
static int directory_created;
static char valid[512];
static char invalid[512];
static char native_invalid[512];
static char empty[512];
static char root[512];
static char decimal[512];
static char missing[512];
static void cleanup_fixtures(void) {
    if (!directory_created) return;
    unlink(valid); unlink(invalid); unlink(native_invalid); unlink(empty); unlink(root); unlink(decimal);
    rmdir(directory);
}
static void fixture(char *path, size_t capacity, const char *name, const char *content) {
    FILE *file;
    size_t written;
    int close_result;
    int length = snprintf(path, capacity, "%s/%s.json", directory, name);
    if (length < 0 || (size_t)length >= capacity) exit(1);
    file = fopen(path, "wb");
    if (file == NULL) exit(1);
    written = fwrite(content, 1, strlen(content), file);
    close_result = fclose(file);
    if (written != strlen(content) || close_result != 0) exit(1);
}

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#condition); exit(1); } ++checks; } while (0)
#define RELEASE_ERROR() do { free(error); error = NULL; } while (0)
#define ENGINE(prefix) do { \
 prefix##_parser *parser = NULL; char *error = NULL; \
 CHECK(prefix##_json_init(NULL, &error) != 0 && error != NULL); RELEASE_ERROR(); \
 CHECK(prefix##_json_init(&parser, NULL) != 0 && parser == NULL); \
 CHECK(prefix##_set_max_depth(NULL, 1) != 0); \
 CHECK(prefix##_set_max_arg_num(NULL, 1) != 0); \
 CHECK(prefix##_set_silence(NULL, 1) != 0); \
 CHECK(prefix##_parse_file(NULL, valid, &error) != 0 && error != NULL); RELEASE_ERROR(); \
 CHECK(prefix##_json_cleanup(NULL) == 0); \
 CHECK(prefix##_json_init(&parser, &error) == 0 && parser != NULL && error == NULL); \
 double invalid_limits[] = {-1.0, 1.5, INFINITY, -INFINITY, NAN, 4294967296.0}; \
 for (unsigned i=0;i<sizeof invalid_limits/sizeof invalid_limits[0];i++) { CHECK(prefix##_set_max_depth(parser, invalid_limits[i]) != 0); CHECK(prefix##_set_max_arg_num(parser, invalid_limits[i]) != 0); } \
 CHECK(prefix##_set_max_depth(parser, UINT_MAX) == 0); CHECK(prefix##_set_max_arg_num(parser, UINT_MAX) == 0); \
 CHECK(prefix##_set_silence(parser, 1) == 0); \
 CHECK(prefix##_parse_file(parser,valid,&error) == 0 && error == NULL); \
 CHECK(prefix##_parse_file(parser,invalid,&error) != 0 && error != NULL); RELEASE_ERROR(); \
 CHECK(prefix##_parse_file(parser,valid,&error) == 0 && error == NULL); \
 CHECK(prefix##_parse_file(parser,NULL,&error) != 0 && error != NULL); RELEASE_ERROR(); \
 CHECK(prefix##_parse_file(parser,valid,NULL) != 0); \
 CHECK(prefix##_set_max_arg_num(parser, 0) == 0); \
 CHECK(prefix##_parse_file(parser,empty,&error) == 0 && error == NULL); \
 CHECK(prefix##_parse_file(parser,valid,&error) != 0 && error != NULL); RELEASE_ERROR(); \
 CHECK(prefix##_set_max_arg_num(parser, 500) == 0); CHECK(prefix##_set_max_depth(parser, 0) == 0); \
 CHECK(prefix##_parse_file(parser,root,&error) == 0 && error == NULL); \
 CHECK(prefix##_parse_file(parser,empty,&error) != 0 && error != NULL); RELEASE_ERROR(); \
 CHECK(prefix##_set_max_depth(parser, 500) == 0); \
 for(unsigned i=0;i<30;i++) { CHECK(prefix##_parse_file(parser,invalid,&error) != 0 && error != NULL); RELEASE_ERROR(); CHECK(prefix##_parse_file(parser,valid,&error) == 0 && error == NULL); } \
 CHECK(prefix##_json_cleanup(parser) == 0); \
 } while(0)

#if HAVE_NEWLOCALE && HAVE_USELOCALE && HAVE_FREELOCALE && HAVE_DUPLOCALE && defined(LC_NUMERIC_MASK)
#define LOCALE_ENGINE(prefix) do { \
    prefix##_parser *parser = NULL; char *error = NULL; \
    locale_t before = uselocale((locale_t)0); \
    char *global_before = strdup(setlocale(LC_NUMERIC, NULL)); \
    CHECK(global_before != NULL); \
    CHECK(prefix##_json_init(&parser, &error) == 0); \
    CHECK(prefix##_set_silence(parser, 1) == 0); \
    CHECK(prefix##_parse_file(parser, decimal, &error) == 0 && error == NULL); \
    CHECK(uselocale((locale_t)0) == before && strcmp(localeconv()->decimal_point, ",") == 0); \
    CHECK(strcmp(setlocale(LC_NUMERIC, NULL), global_before) == 0); \
    CHECK(prefix##_parse_file(parser, invalid, &error) != 0 && error != NULL); RELEASE_ERROR(); \
    CHECK(uselocale((locale_t)0) == before && strcmp(localeconv()->decimal_point, ",") == 0); \
    CHECK(prefix##_parse_file(parser, native_invalid, &error) != 0 && error != NULL); RELEASE_ERROR(); \
    CHECK(uselocale((locale_t)0) == before && strcmp(localeconv()->decimal_point, ",") == 0); \
    CHECK(prefix##_parse_file(parser, missing, &error) != 0 && error != NULL); RELEASE_ERROR(); \
    CHECK(uselocale((locale_t)0) == before && strcmp(localeconv()->decimal_point, ",") == 0); \
    FILE *capture = tmpfile(); int saved_stdout = dup(STDOUT_FILENO); \
    CHECK(capture != NULL && saved_stdout >= 0 && fflush(stdout) == 0); \
    CHECK(dup2(fileno(capture), STDOUT_FILENO) >= 0); \
    CHECK(prefix##_set_silence(parser, 0) == 0); \
    int parse_result = prefix##_parse_file(parser, decimal, &error); \
    CHECK(fflush(stdout) == 0 && dup2(saved_stdout, STDOUT_FILENO) >= 0); close(saved_stdout); \
    CHECK(parse_result == 0 && error == NULL); \
    CHECK(uselocale((locale_t)0) == before && strcmp(localeconv()->decimal_point, ",") == 0); \
    CHECK(strcmp(setlocale(LC_NUMERIC, NULL), global_before) == 0); \
    rewind(capture); char output[128] = {0}; size_t count = fread(output, 1, sizeof(output), capture); \
    const char *expected = "a: 1.25\nb: 1e-3\nneg: -0\n"; \
    CHECK(count == strlen(expected) && memcmp(output, expected, count) == 0); fclose(capture); \
    CHECK(prefix##_json_cleanup(parser) == 0); free(global_before); \
} while (0)

static void locale_engines(void) {
#if HAVE_JSONC
    LOCALE_ENGINE(jsonc);
#endif
#if HAVE_JANSSON
    LOCALE_ENGINE(jansson);
#endif
#if HAVE_CJSON
    LOCALE_ENGINE(cjson);
#endif
#if HAVE_YYJSON
    LOCALE_ENGINE(yyjson);
#endif
}

static void locale_checks(void) {
    static const char *names[] = {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "fr_FR.utf8"};
    char *original = strdup(setlocale(LC_NUMERIC, NULL));
    const char *selected = NULL;
    locale_t caller_locale;
    locale_t previous;
    CHECK(original != NULL);
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (setlocale(LC_NUMERIC, names[i]) != NULL && strcmp(localeconv()->decimal_point, ",") == 0) {
            selected = names[i]; break;
        }
    }
    if (selected == NULL) {
        CHECK(setlocale(LC_NUMERIC, original) != NULL);
        free(original);
        puts("SKIP numeric locale checks: no installed decimal-comma locale");
        return;
    }
    /* Global comma locale, then independently selected per-thread comma locale.
     * Production adapters must leave both caller settings untouched. */
    locale_engines();
    CHECK(setlocale(LC_NUMERIC, "C") != NULL);
    caller_locale = newlocale(LC_NUMERIC_MASK, selected, (locale_t)0);
    CHECK(caller_locale != (locale_t)0);
    previous = uselocale(caller_locale);
    CHECK(previous != (locale_t)0);
    locale_engines();
    CHECK(uselocale(previous) == caller_locale);
    freelocale(caller_locale);
    CHECK(setlocale(LC_NUMERIC, original) != NULL);
    free(original);
}
#else
static void locale_checks(void) {
    puts("SKIP numeric locale checks: POSIX thread locales unavailable");
}
#endif

int main(void) {
#if !HAVE_JSONC && !HAVE_JANSSON && !HAVE_CJSON && !HAVE_YYJSON
    puts("SKIP DOM C API checks: no DOM engines enabled");
    return 77;
#endif
    CHECK(atexit(cleanup_fixtures) == 0);
    CHECK(mkdtemp(directory) != NULL);
    directory_created = 1;
    fixture(valid, sizeof(valid), "valid", "{\"x\":[1,true,null,{\"k\":\"value\"}]}");
    fixture(invalid, sizeof(invalid), "invalid", "{\"x\":\"\\q\"}");
    fixture(native_invalid, sizeof(native_invalid), "native-invalid", "{\"a\":1,}");
    fixture(empty, sizeof(empty), "empty", "{}");
    fixture(root, sizeof(root), "root", "42");
    fixture(decimal, sizeof(decimal), "decimal", "{\"a\":1.25,\"b\":1e-3,\"neg\":-0}");
    CHECK(snprintf(missing, sizeof(missing), "%s/missing.json", directory) > 0);
#if HAVE_JSONC
    ENGINE(jsonc);
#endif
#if HAVE_JANSSON
    ENGINE(jansson);
#endif
#if HAVE_CJSON
    ENGINE(cjson);
#endif
#if HAVE_YYJSON
    ENGINE(yyjson);
#endif
    locale_checks();
    printf("%u direct DOM C API checks passed\n", checks);
    return 0;
}
