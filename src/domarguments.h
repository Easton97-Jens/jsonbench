#ifndef JSONBENCH_DOM_ARGUMENTS_H
#define JSONBENCH_DOM_ARGUMENTS_H

/* Shared adapter plumbing, not a replacement JSON parser. Native libraries
 * still validate grammar and build their own DOM. The lexical preflight bounds
 * allocations and checks boundaries that some native DOM APIs cannot retain. */
#include "../config.h"
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned int max_depth;
    unsigned int max_args;
    int silence;
} dom_settings;

static void dom_settings_init(dom_settings *settings) {
    settings->max_depth = 500;
    settings->max_args = 500;
    settings->silence = 0;
}

static int dom_error(char **error, const char *message) {
    if (error != NULL) *error = strdup(message);
    return 2;
}

#if HAVE_NEWLOCALE && HAVE_USELOCALE && HAVE_FREELOCALE && HAVE_DUPLOCALE && defined(LC_NUMERIC_MASK)
typedef struct {
    locale_t current;
    locale_t previous;
} dom_locale_guard;

/* JSON numbers always use '.', regardless of the embedding application's
 * process or thread locale. Change only this thread and restore its previous
 * locale on every exit; never call the process-wide setlocale here. */
static int dom_locale_begin(dom_locale_guard *guard, char **error) {
    locale_t base = duplocale(uselocale((locale_t)0));
    if (base == (locale_t)0) return dom_error(error, "Unable to duplicate caller numeric locale");
    guard->current = newlocale(LC_NUMERIC_MASK, "C", base);
    if (guard->current == (locale_t)0) {
        freelocale(base);
        return dom_error(error, "Unable to allocate JSON numeric locale");
    }
    guard->previous = uselocale(guard->current);
    if (guard->previous == (locale_t)0) {
        freelocale(guard->current); guard->current = (locale_t)0;
        return dom_error(error, "Unable to activate JSON numeric thread locale");
    }
    return 0;
}

static int dom_locale_finish(dom_locale_guard *guard, int result, char **error) {
    if (guard->current != (locale_t)0) {
        /* Do not free a locale if an unexpected restore failure leaves it
         * active. Valid caller-owned previous locales restore successfully. */
        if (uselocale(guard->previous) == (locale_t)0)
            return result != 0 ? result : dom_error(error, "Unable to restore numeric thread locale");
        freelocale(guard->current); guard->current = (locale_t)0;
    }
    return result;
}
#else
typedef struct { int unused; } dom_locale_guard;
static int dom_locale_begin(dom_locale_guard *guard, char **error) {
    const struct lconv *current = localeconv();
    (void)guard;
    /* A platform without per-thread locales remains usable in an existing
     * JSON-compatible numeric locale. Never change its process-wide locale. */
    if (current != NULL && current->decimal_point != NULL &&
        strcmp(current->decimal_point, ".") == 0) return 0;
    return dom_error(error, "Native numeric DOM parsing requires POSIX thread locale support");
}
static int dom_locale_finish(dom_locale_guard *guard, int result, char **error) {
    (void)guard; (void)error;
    return result;
}
#endif

static int dom_set_limit(double value, unsigned int *limit) {
    if (limit == NULL || !isfinite(value) || value < 0 ||
        value > UINT_MAX || floor(value) != value) return -1;
    *limit = (unsigned int)value;
    return 0;
}

/* Read regular files and pipes without seek/ftell assumptions. DOM adapters
 * necessarily retain document-sized memory, unlike the SAX adapters. */
static int dom_read_file(const char *filename, char **content, size_t *length,
                         char **error) {
    FILE *file = fopen(filename, "rb");
    size_t used = 0;
    size_t capacity = 16384;
    char *buffer;
    if (file == NULL) return dom_error(error, "Unable to open JSON file");
    buffer = (char *)malloc(capacity);
    if (buffer == NULL) {
        fclose(file);
        return dom_error(error, "Unable to allocate JSON input");
    }
    for (;;) {
        size_t count;
        if (used == capacity - 1) {
            char *grown;
            if (capacity > SIZE_MAX / 2) {
                free(buffer); fclose(file);
                return dom_error(error, "JSON input size overflow");
            }
            capacity *= 2;
            grown = (char *)realloc(buffer, capacity);
            if (grown == NULL) {
                free(buffer); fclose(file);
                return dom_error(error, "Unable to allocate JSON input");
            }
            buffer = grown;
        }
        count = fread(buffer + used, 1, capacity - used - 1, file);
        used += count;
        if (count == 0) {
            if (ferror(file)) {
                free(buffer); fclose(file);
                return dom_error(error, "Unable to read JSON file");
            }
            break;
        }
    }
    if (fclose(file) != 0) {
        free(buffer);
        return dom_error(error, "Unable to close JSON file");
    }
    buffer[used] = '\0';
    *content = buffer; *length = used;
    return 0;
}

/* Numeric DOM libraries need the same argument validation, file input, and
 * thread-locale lifetime. Native parsing and native resource cleanup stay in
 * their adapters. A read failure restores the locale before returning. */
static inline int dom_begin_numeric_file(const void *parser, const char *filename,
                                          const char *invalid_message, dom_locale_guard *locale,
                                          char **content, size_t *length, char **error) {
    int result;
    if (error == NULL) return -1;
    *error = NULL;
    if (parser == NULL || filename == NULL) return dom_error(error, invalid_message);
    result = dom_locale_begin(locale, error);
    if (result != 0) return result;
    result = dom_read_file(filename, content, length, error);
    if (result != 0) return dom_locale_finish(locale, result, error);
    return 0;
}

static int dom_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int dom_hex(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int dom_code_unit(const char *data, size_t length, size_t *position,
                          unsigned int *code) {
    unsigned int result = 0;
    if (length - *position < 4) return 0;
    for (size_t i = 0; i < 4; ++i) {
        int digit = dom_hex((unsigned char)data[(*position)++]);
        if (digit < 0) return 0;
        result = result * 16 + (unsigned int)digit;
    }
    *code = result;
    return 1;
}

/* Strict scalar UTF-8, excluding overlong forms, surrogates and > U+10ffff. */
static int dom_utf8(const char *data, size_t length, size_t *position) {
    unsigned char first = (unsigned char)data[*position];
    size_t count;
    unsigned int code;
    unsigned int minimum;
    if (first < 0x80) { ++*position; return 1; }
    if (first >= 0xc2 && first <= 0xdf) { count = 2; code = first & 31; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { count = 3; code = first & 15; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { count = 4; code = first & 7; minimum = 0x10000; }
    else return 0;
    if (length - *position < count) return 0;
    for (size_t i = 1; i < count; ++i) {
        unsigned char byte = (unsigned char)data[*position + i];
        if ((byte & 0xc0) != 0x80) return 0;
        code = code * 64 + (byte & 63);
    }
    if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return 0;
    *position += count;
    return 1;
}

enum { DOM_NUL_ALLOWED = 0, DOM_NUL_KEY_FORBIDDEN = 1, DOM_NUL_FORBIDDEN = 2 };
enum { DOM_NUM_RAW = 0, DOM_NUM_INT64_UINT64 = 1, DOM_NUM_DOUBLE = 2, DOM_NUM_SIGNED = 3 };
enum { DOM_NOT_NUMBER = 0, DOM_INTEGER = 1, DOM_REAL = 2, DOM_ANY_NUMBER = 3 };

/* Native representation limits belong together; settings hold user limits. */
typedef struct {
    int nul_policy;
    int number_policy;
} dom_input_policy;

typedef struct {
    const char *data;
    size_t length;
    int kind;
} dom_number_span;

typedef struct {
    dom_number_span *spans;
    size_t count;
    size_t capacity;
} dom_numbers;

static int dom_remember_number(dom_numbers *numbers, const char *data,
                                size_t length, int kind, char **error) {
    dom_number_span *grown;
    if (numbers == NULL) return 0;
    if (numbers->count == numbers->capacity) {
        size_t next = numbers->capacity ? numbers->capacity * 2 : 32;
        if (next < numbers->capacity || next > SIZE_MAX / sizeof(*grown))
            return dom_error(error, "Numeric source span allocation overflow");
        grown = (dom_number_span *)realloc(numbers->spans, next * sizeof(*grown));
        if (grown == NULL) return dom_error(error, "Unable to allocate numeric source spans");
        numbers->spans = grown; numbers->capacity = next;
    }
    numbers->spans[numbers->count].data = data;
    numbers->spans[numbers->count].length = length;
    numbers->spans[numbers->count].kind = kind;
    ++numbers->count;
    return 0;
}

/* Tokens are checked before native parsing so hostile long strings/numbers
 * and deep structures cannot first allocate an unbounded native token/stack.
 * No structure acceptance depends on this scan: native parsing follows. */
typedef struct {
    const char *data;
    size_t length;
    size_t position;
    size_t depth;
    size_t arguments;
    const dom_settings *settings;
    int nul_policy;
    int number_policy;
    size_t *keys;
    dom_numbers *numbers;
    char **error;
} dom_scan;

static int dom_scan_argument(dom_scan *scan) {
    ++scan->arguments;
    if (scan->arguments > scan->settings->max_args)
        return dom_error(scan->error, "Argument number limit exceeded");
    return 0;
}

static size_t dom_codepoint_length(unsigned int code) {
    if (code < 0x80) return 1;
    if (code < 0x800) return 2;
    return 3;
}

static int dom_scan_surrogate_pair(dom_scan *scan, size_t *decoded) {
    unsigned int low;
    if (scan->length - scan->position < 2 || scan->data[scan->position] != '\\' ||
        scan->data[scan->position + 1] != 'u')
        return dom_error(scan->error, "Unpaired Unicode surrogate");
    scan->position += 2;
    if (!dom_code_unit(scan->data, scan->length, &scan->position, &low) ||
        low < 0xdc00 || low > 0xdfff)
        return dom_error(scan->error, "Invalid Unicode surrogate pair");
    *decoded += 4;
    return 0;
}

static int dom_scan_unicode_escape(dom_scan *scan, size_t *decoded, int *contains_nul) {
    unsigned int code;
    if (!dom_code_unit(scan->data, scan->length, &scan->position, &code))
        return dom_error(scan->error, "Invalid Unicode escape");
    if (code >= 0xd800 && code <= 0xdbff) return dom_scan_surrogate_pair(scan, decoded);
    if (code >= 0xdc00 && code <= 0xdfff)
        return dom_error(scan->error, "Unpaired Unicode surrogate");
    if (code == 0) *contains_nul = 1;
    *decoded += dom_codepoint_length(code);
    return 0;
}

static int dom_scan_escape(dom_scan *scan, size_t *decoded, int *contains_nul) {
    unsigned char c;
    if (scan->position == scan->length) return dom_error(scan->error, "Truncated JSON escape");
    c = (unsigned char)scan->data[scan->position];
    ++scan->position;
    if (c == 'u') return dom_scan_unicode_escape(scan, decoded, contains_nul);
    switch (c) {
        case '"': case '\\': case '/': case 'b': case 'f': case 'n': case 'r': case 't':
            ++*decoded;
            return 0;
        default:
            return dom_error(scan->error, "Invalid JSON escape");
    }
}

static int dom_scan_string_character(dom_scan *scan, unsigned char c,
                                      size_t *decoded, int *contains_nul) {
    size_t start;
    if (c < 0x20) return dom_error(scan->error, "Invalid control character in JSON string");
    if (c == '\\') return dom_scan_escape(scan, decoded, contains_nul);
    if (c < 0x80) {
        ++*decoded;
        return 0;
    }
    --scan->position;
    start = scan->position;
    if (!dom_utf8(scan->data, scan->length, &scan->position))
        return dom_error(scan->error, "Invalid UTF-8 in JSON string");
    *decoded += scan->position - start;
    return 0;
}

static int dom_scan_string_role(dom_scan *scan, int contains_nul) {
    size_t end = scan->position;
    while (end < scan->length && dom_space((unsigned char)scan->data[end])) ++end;
    if (end < scan->length && scan->data[end] == ':') {
        ++*scan->keys;
        if (contains_nul && scan->nul_policy != DOM_NUL_ALLOWED)
            return dom_error(scan->error, "Native DOM cannot preserve NUL in object keys");
        return 0;
    }
    if (contains_nul && scan->nul_policy == DOM_NUL_FORBIDDEN)
        return dom_error(scan->error, "Native DOM cannot preserve NUL in string values");
    return dom_scan_argument(scan);
}

static int dom_scan_string(dom_scan *scan) {
    size_t decoded = 0;
    int contains_nul = 0;
    int closed = 0;
    ++scan->position;
    while (scan->position < scan->length) {
        unsigned char c = (unsigned char)scan->data[scan->position];
        int result;
        ++scan->position;
        if (c == '"') {
            closed = 1;
            break;
        }
        result = dom_scan_string_character(scan, c, &decoded, &contains_nul);
        if (result != 0) return result;
        if (decoded >= JSON_STRING_SIZE)
            return dom_error(scan->error, "Argument name or value too long");
    }
    if (!closed) return dom_error(scan->error, "Truncated JSON string");
    return dom_scan_string_role(scan, contains_nul);
}

static int dom_digit(unsigned char c) {
    return c >= '0' && c <= '9';
}

static void dom_scan_digits(dom_scan *scan) {
    while (scan->position < scan->length && dom_digit((unsigned char)scan->data[scan->position]))
        ++scan->position;
}

static int dom_scan_integer_part(dom_scan *scan) {
    if (scan->position == scan->length || !dom_digit((unsigned char)scan->data[scan->position]))
        return dom_error(scan->error, "Invalid JSON number");
    if (scan->data[scan->position] == '0') ++scan->position;
    else dom_scan_digits(scan);
    return 0;
}

static int dom_scan_fraction(dom_scan *scan, int *real) {
    size_t start;
    if (scan->position == scan->length || scan->data[scan->position] != '.') return 0;
    *real = 1;
    ++scan->position;
    start = scan->position;
    dom_scan_digits(scan);
    if (scan->position == start) return dom_error(scan->error, "Invalid JSON fraction");
    return 0;
}

static int dom_scan_exponent(dom_scan *scan, int *real) {
    size_t start;
    unsigned char c;
    if (scan->position == scan->length) return 0;
    c = (unsigned char)scan->data[scan->position];
    if (c != 'e' && c != 'E') return 0;
    *real = 1;
    ++scan->position;
    if (scan->position < scan->length &&
        (scan->data[scan->position] == '+' || scan->data[scan->position] == '-')) ++scan->position;
    start = scan->position;
    dom_scan_digits(scan);
    if (scan->position == start) return dom_error(scan->error, "Invalid JSON exponent");
    return 0;
}

static const char *dom_integer_maximum(int policy, int negative) {
    if (policy == DOM_NUM_DOUBLE) return "9007199254740992";
    if (negative) return "9223372036854775808";
    if (policy == DOM_NUM_SIGNED) return "9223372036854775807";
    return "18446744073709551615";
}

static int dom_scan_integer_range(dom_scan *scan, size_t digits, int negative) {
    const char *maximum = dom_integer_maximum(scan->number_policy, negative);
    size_t integer_length = scan->position - digits;
    size_t maximum_length = strlen(maximum);
    if (integer_length > maximum_length ||
        (integer_length == maximum_length && memcmp(scan->data + digits, maximum, maximum_length) > 0))
        return dom_error(scan->error, "JSON integer is outside the native DOM's lossless range");
    return 0;
}

static int dom_scan_real_range(dom_scan *scan, size_t start, size_t length) {
    char number[JSON_STRING_SIZE];
    char *end;
    double value;
    memcpy(number, scan->data + start, length);
    number[length] = '\0';
    errno = 0;
    value = strtod(number, &end);
    if (*end != '\0' || !isfinite(value) || (errno == ERANGE && value == 0.0))
        return dom_error(scan->error, "JSON real is outside the native DOM's finite range");
    return 0;
}

static int dom_scan_number_separator(const dom_scan *scan) {
    unsigned char c;
    if (scan->position == scan->length) return 1;
    c = (unsigned char)scan->data[scan->position];
    return dom_space(c) || c == ',' || c == ']' || c == '}';
}

static int dom_scan_number_range(dom_scan *scan, size_t start, size_t digits,
                                 size_t length, int real, int negative) {
    if (scan->number_policy == DOM_NUM_RAW) return 0;
    if (real) return dom_scan_real_range(scan, start, length);
    return dom_scan_integer_range(scan, digits, negative);
}

static int dom_scan_number(dom_scan *scan) {
    size_t start = scan->position;
    size_t digits;
    size_t length;
    int negative = scan->data[start] == '-';
    int real = 0;
    int result;
    if (negative) ++scan->position;
    digits = scan->position;
    result = dom_scan_integer_part(scan);
    if (result != 0) return result;
    result = dom_scan_fraction(scan, &real);
    if (result != 0) return result;
    result = dom_scan_exponent(scan, &real);
    if (result != 0) return result;
    if (!dom_scan_number_separator(scan)) return dom_error(scan->error, "Invalid JSON number separator");
    length = scan->position - start;
    if (length >= JSON_STRING_SIZE) return dom_error(scan->error, "Argument value too long");
    result = dom_scan_number_range(scan, start, digits, length, real, negative);
    if (result != 0) return result;
    result = dom_scan_argument(scan);
    if (result != 0) return result;
    return dom_remember_number(scan->numbers, scan->data + start, length,
                               real ? DOM_REAL : DOM_INTEGER, scan->error);
}

static const char *dom_literal(unsigned char c) {
    if (c == 't') return "true";
    if (c == 'f') return "false";
    return "null";
}

static int dom_scan_literal(dom_scan *scan, unsigned char c) {
    const char *literal = dom_literal(c);
    size_t length = strlen(literal);
    if (scan->length - scan->position < length ||
        memcmp(scan->data + scan->position, literal, length) != 0)
        return dom_error(scan->error, "Invalid JSON literal");
    scan->position += length;
    return dom_scan_argument(scan);
}

static int dom_scan_token(dom_scan *scan) {
    unsigned char c = (unsigned char)scan->data[scan->position];
    if (dom_space(c) || c == ':' || c == ',') {
        ++scan->position;
        return 0;
    }
    switch (c) {
        case '{': case '[':
            ++scan->depth;
            if (scan->depth > scan->settings->max_depth)
                return dom_error(scan->error, "JSON depth limit exceeded");
            ++scan->position;
            return 0;
        case '}': case ']':
            if (scan->depth > 0) --scan->depth;
            ++scan->position;
            return 0;
        case '"':
            return dom_scan_string(scan);
        case 't': case 'f': case 'n':
            return dom_scan_literal(scan, c);
        default:
            if (c == '-' || dom_digit(c)) return dom_scan_number(scan);
            return dom_error(scan->error, "Unexpected character in JSON input");
    }
}

static int dom_preflight_impl(const char *data, size_t length, const dom_settings *settings,
                               const dom_input_policy *policy, size_t *keys,
                               dom_numbers *numbers, char **error) {
    dom_scan scan;
    memset(&scan, 0, sizeof(scan));
    scan.data = data;
    scan.length = length;
    scan.settings = settings;
    scan.nul_policy = policy->nul_policy;
    scan.number_policy = policy->number_policy;
    scan.keys = keys;
    scan.numbers = numbers;
    scan.error = error;
    *keys = 0;
    while (scan.position < scan.length) {
        int result = dom_scan_token(&scan);
        if (result != 0) return result;
    }
    return 0;
}

static inline int dom_preflight(const char *data, size_t length, const dom_settings *settings,
                                 const dom_input_policy *policy, size_t *keys, char **error) {
    dom_locale_guard locale = {0};
    int result;
    if (policy->number_policy == DOM_NUM_RAW)
        return dom_preflight_impl(data, length, settings, policy, keys, NULL, error);
    result = dom_locale_begin(&locale, error);
    if (result != 0) return result;
    result = dom_preflight_impl(data, length, settings, policy, keys, NULL, error);
    return dom_locale_finish(&locale, result, error);
}

/* Borrow numeric tokens from the still-live input. Only native parse success
 * makes these eligible for output; source-order native traversal checks their
 * number kind and total count, including during silent validation. Native
 * range checks remain in effect, even though output avoids binary reformatting. */
static inline int dom_preflight_numbers(const char *data, size_t length,
                                         const dom_settings *settings, const dom_input_policy *policy,
                                         size_t *keys, dom_numbers *numbers, char **error) {
    dom_locale_guard locale = {0};
    int result;
    if (policy->number_policy == DOM_NUM_RAW)
        return dom_preflight_impl(data, length, settings, policy, keys, numbers, error);
    result = dom_locale_begin(&locale, error);
    if (result != 0) return result;
    result = dom_preflight_impl(data, length, settings, policy, keys, numbers, error);
    return dom_locale_finish(&locale, result, error);
}

enum { DOM_SCALAR = 0, DOM_OBJECT = 1, DOM_ARRAY = 2 };
/* Large/aligned enough for native cursor structs; adapters use memcpy. */
typedef union {
    void *pointer;
    size_t index;
    void *slots[8];
    long double alignment;
} dom_cursor;

typedef struct {
    const char *data;
    size_t length;
    char number[128];
    int number_kind;
} dom_value;

typedef struct {
    int (*kind)(void *node);
    int (*child)(void *node, dom_cursor *cursor, int first,
                  void **child, const char **key, size_t *key_length);
    int (*scalar)(void *node, dom_value *value, char **error);
} dom_adapter;

typedef struct {
    void *node;
    dom_cursor cursor;
    int first;
    size_t prefix_before;
    size_t key_length;
    char *key_before;
} dom_frame;

typedef struct {
    dom_frame *frames;
    size_t depth;
    size_t capacity;
    size_t prefix_length;
    size_t key_length;
    unsigned int arguments;
    size_t number_position;
    char prefix[JSON_STRING_SIZE];
    char key[JSON_STRING_SIZE];
    void *node;
    const dom_adapter *adapter;
    const dom_settings *settings;
    const dom_numbers *numbers;
    size_t *object_keys;
    char **error;
} dom_walk_state;

static int dom_walk_grow_frames(dom_walk_state *state) {
    size_t next;
    dom_frame *grown;
    if (state->depth < state->capacity) return 0;
    next = state->capacity ? state->capacity * 2 : 16;
    if (next < state->capacity || next > SIZE_MAX / sizeof(*state->frames))
        return dom_error(state->error, "DOM traversal stack overflow");
    grown = (dom_frame *)realloc(state->frames, next * sizeof(*state->frames));
    if (grown == NULL) return dom_error(state->error, "Unable to allocate DOM traversal stack");
    state->frames = grown;
    state->capacity = next;
    return 0;
}

static size_t dom_walk_prefix_size(const dom_walk_state *state, int kind) {
    if (kind == DOM_ARRAY && state->depth == 0) return 5;
    return state->prefix_length + (state->prefix_length ? 1 : 0) + state->key_length;
}

static void dom_walk_push_prefix(dom_walk_state *state, int kind) {
    if (kind == DOM_ARRAY && state->depth == 0) {
        memcpy(state->prefix, "array", 5);
        memcpy(state->key, "array", 5);
        state->prefix_length = 5;
        state->key_length = 5;
        return;
    }
    if (state->prefix_length) {
        state->prefix[state->prefix_length] = '.';
        ++state->prefix_length;
    }
    memcpy(state->prefix + state->prefix_length, state->key, state->key_length);
    state->prefix_length += state->key_length;
}

static int dom_walk_begin(dom_walk_state *state, int kind) {
    dom_frame *frame;
    int result;
    if (state->depth >= state->settings->max_depth)
        return dom_error(state->error, "JSON depth limit exceeded");
    if (dom_walk_prefix_size(state, kind) >= JSON_STRING_SIZE)
        return dom_error(state->error, "Argument name too long");
    result = dom_walk_grow_frames(state);
    if (result != 0) return result;
    frame = &state->frames[state->depth];
    memset(frame, 0, sizeof(*frame));
    frame->node = state->node;
    frame->first = 1;
    frame->prefix_before = state->prefix_length;
    frame->key_length = state->key_length;
    frame->key_before = (char *)malloc(state->key_length + 1);
    if (frame->key_before == NULL) return dom_error(state->error, "Unable to allocate DOM traversal key");
    memcpy(frame->key_before, state->key, state->key_length);
    dom_walk_push_prefix(state, kind);
    ++state->depth;
    return 0;
}

static void dom_walk_pop(dom_walk_state *state) {
    dom_frame *frame = &state->frames[state->depth - 1];
    state->prefix_length = frame->prefix_before;
    state->key_length = frame->key_length;
    memcpy(state->key, frame->key_before, state->key_length);
    free(frame->key_before);
    --state->depth;
}

static int dom_walk_number_source(dom_walk_state *state, dom_value *value) {
    const dom_number_span *span;
    if (state->numbers == NULL || value->number_kind == DOM_NOT_NUMBER) return 0;
    if (state->number_position >= state->numbers->count)
        return dom_error(state->error, "Native DOM has unexpected numeric arguments");
    span = &state->numbers->spans[state->number_position];
    ++state->number_position;
    if (value->number_kind != DOM_ANY_NUMBER && value->number_kind != span->kind)
        return dom_error(state->error, "Native DOM numeric type differs from source token");
    value->data = span->data;
    value->length = span->length;
    return 0;
}

static int dom_walk_write(const dom_walk_state *state, const dom_value *value) {
    struct {
        const char *data;
        size_t length;
    } parts[] = {
        {state->prefix, state->prefix_length},
        {".", state->prefix_length ? 1U : 0U},
        {state->key, state->key_length},
        {": ", 2},
        {value->data, value->length},
        {"\n", 1}
    };
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        if (parts[i].length == 0) continue;
        if (fwrite(parts[i].data, 1, parts[i].length, stdout) != parts[i].length)
            return dom_error(state->error, "Unable to write JSON argument output");
    }
    return 0;
}

static int dom_walk_argument(dom_walk_state *state) {
    dom_value value;
    size_t name_length = state->prefix_length + (state->prefix_length ? 1 : 0) + state->key_length;
    int result;
    if (state->arguments >= state->settings->max_args)
        return dom_error(state->error, "Argument number limit exceeded");
    if (name_length >= JSON_STRING_SIZE) return dom_error(state->error, "Argument name too long");
    memset(&value, 0, sizeof(value));
    result = state->adapter->scalar(state->node, &value, state->error);
    if (result != 0) return result;
    result = dom_walk_number_source(state, &value);
    if (result != 0) return result;
    if (value.length >= JSON_STRING_SIZE) return dom_error(state->error, "Argument value too long");
    ++state->arguments;
    if (state->settings->silence) return 0;
    return dom_walk_write(state, &value);
}

static int dom_walk_enter(dom_walk_state *state) {
    int kind = state->adapter->kind(state->node);
    if (kind == DOM_SCALAR) return dom_walk_argument(state);
    return dom_walk_begin(state, kind);
}

static int dom_walk_object_child(dom_walk_state *state, const char *key,
                                  size_t length, int *entering) {
    ++*state->object_keys;
    if (length >= JSON_STRING_SIZE) return dom_error(state->error, "Argument name too long");
    memcpy(state->key, key, length);
    state->key_length = length;
    *entering = 1;
    return 0;
}

static int dom_walk_next(dom_walk_state *state, int *entering) {
    *entering = 0;
    while (state->depth) {
        dom_frame *frame = &state->frames[state->depth - 1];
        const char *child_key = NULL;
        size_t child_key_length = 0;
        if (state->adapter->child(frame->node, &frame->cursor, frame->first,
                                  &state->node, &child_key, &child_key_length)) {
            frame->first = 0;
            if (state->adapter->kind(frame->node) == DOM_OBJECT)
                return dom_walk_object_child(state, child_key, child_key_length, entering);
            *entering = 1;
            return 0;
        }
        dom_walk_pop(state);
    }
    return 0;
}

static int dom_walk_finish(dom_walk_state *state, int result) {
    while (state->depth) dom_walk_pop(state);
    free(state->frames);
    if (result != 0) return result;
    if (state->numbers != NULL && state->number_position != state->numbers->count)
        return dom_error(state->error, "Native DOM omitted numeric arguments from source");
    if (!state->settings->silence && fflush(stdout) != 0)
        return dom_error(state->error, "Unable to flush JSON argument output");
    return 0;
}

static int dom_walk_impl(void *root, const dom_adapter *adapter,
                          const dom_settings *settings, size_t *object_keys,
                          const dom_numbers *numbers, char **error) {
    dom_walk_state state;
    int entering = 1;
    int result = 0;
    memset(&state, 0, sizeof(state));
    state.node = root;
    state.adapter = adapter;
    state.settings = settings;
    state.numbers = numbers;
    state.object_keys = object_keys;
    state.error = error;
    *object_keys = 0;
    while (entering && result == 0) {
        result = dom_walk_enter(&state);
        if (result == 0) result = dom_walk_next(&state, &entering);
    }
    return dom_walk_finish(&state, result);
}

static inline int dom_walk(void *root, const dom_adapter *adapter,
                            const dom_settings *settings, size_t *object_keys, char **error) {
    return dom_walk_impl(root, adapter, settings, object_keys, NULL, error);
}

static inline int dom_walk_numbers(void *root, const dom_adapter *adapter,
                                    const dom_settings *settings, size_t *object_keys,
                                    const dom_numbers *numbers, char **error) {
    return dom_walk_impl(root, adapter, settings, object_keys, numbers, error);
}

#endif
