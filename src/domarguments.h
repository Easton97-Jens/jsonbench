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
    unsigned int max_depth, max_args;
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
typedef struct { locale_t current, previous; } dom_locale_guard;

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
    size_t used = 0, capacity = 16384;
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
    size_t i;
    if (length - *position < 4) return 0;
    for (i = 0; i < 4; ++i) {
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
    size_t count, i;
    unsigned int code, minimum;
    if (first < 0x80) { ++*position; return 1; }
    if (first >= 0xc2 && first <= 0xdf) { count = 2; code = first & 31; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { count = 3; code = first & 15; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { count = 4; code = first & 7; minimum = 0x10000; }
    else return 0;
    if (length - *position < count) return 0;
    for (i = 1; i < count; ++i) {
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

typedef struct {
    const char *data;
    size_t length;
    int kind;
} dom_number_span;

typedef struct {
    dom_number_span *spans;
    size_t count, capacity;
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
    numbers->spans[numbers->count++].kind = kind;
    return 0;
}

/* Tokens are checked before native parsing so hostile long strings/numbers
 * and deep structures cannot first allocate an unbounded native token/stack.
 * No structure acceptance depends on this scan: native parsing follows. */
static int dom_preflight_impl(const char *data, size_t length, const dom_settings *settings,
                               int nul_policy, int number_policy, size_t *keys,
                               dom_numbers *numbers, char **error) {
    size_t position = 0, depth = 0, arguments = 0;
    *keys = 0;
    while (position < length) {
        unsigned char c = (unsigned char)data[position];
        if (dom_space(c) || c == ':' || c == ',') { ++position; continue; }
        if (c == '{' || c == '[') {
            if (++depth > settings->max_depth) return dom_error(error, "JSON depth limit exceeded");
            ++position; continue;
        }
        if (c == '}' || c == ']') { if (depth > 0) --depth; ++position; continue; }
        if (c == '"') {
            size_t decoded = 0, end;
            int closed = 0, contains_nul = 0;
            ++position;
            while (position < length) {
                c = (unsigned char)data[position++];
                if (c == '"') { closed = 1; break; }
                if (c < 0x20) return dom_error(error, "Invalid control character in JSON string");
                if (c == '\\') {
                    unsigned int code;
                    if (position == length) return dom_error(error, "Truncated JSON escape");
                    c = (unsigned char)data[position++];
                    if (c == 'u') {
                        if (!dom_code_unit(data, length, &position, &code)) return dom_error(error, "Invalid Unicode escape");
                        if (code >= 0xd800 && code <= 0xdbff) {
                            unsigned int low;
                            if (length - position < 2 || data[position] != '\\' || data[position + 1] != 'u')
                                return dom_error(error, "Unpaired Unicode surrogate");
                            position += 2;
                            if (!dom_code_unit(data, length, &position, &low) || low < 0xdc00 || low > 0xdfff)
                                return dom_error(error, "Invalid Unicode surrogate pair");
                            decoded += 4;
                        } else {
                            if (code >= 0xdc00 && code <= 0xdfff) return dom_error(error, "Unpaired Unicode surrogate");
                            if (code == 0) contains_nul = 1;
                            decoded += code < 0x80 ? 1 : code < 0x800 ? 2 : 3;
                        }
                    } else if (c == '"' || c == '\\' || c == '/' || c == 'b' || c == 'f' || c == 'n' || c == 'r' || c == 't') {
                        ++decoded;
                    } else return dom_error(error, "Invalid JSON escape");
                } else if (c >= 0x80) {
                    size_t start = --position;
                    if (!dom_utf8(data, length, &position)) return dom_error(error, "Invalid UTF-8 in JSON string");
                    decoded += position - start;
                } else ++decoded;
                if (decoded >= JSON_STRING_SIZE) return dom_error(error, "Argument name or value too long");
            }
            if (!closed) return dom_error(error, "Truncated JSON string");
            end = position;
            while (end < length && dom_space((unsigned char)data[end])) ++end;
            if (end < length && data[end] == ':') {
                ++*keys;
                if (contains_nul && nul_policy != DOM_NUL_ALLOWED) return dom_error(error, "Native DOM cannot preserve NUL in object keys");
            } else {
                if (contains_nul && nul_policy == DOM_NUL_FORBIDDEN) return dom_error(error, "Native DOM cannot preserve NUL in string values");
                if (++arguments > settings->max_args) return dom_error(error, "Argument number limit exceeded");
            }
            continue;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            size_t start = position, digits, token_length;
            int negative = c == '-', real = 0;
            if (negative) ++position;
            digits = position;
            if (position == length || data[position] < '0' || data[position] > '9') return dom_error(error, "Invalid JSON number");
            if (data[position] == '0') ++position;
            else while (position < length && data[position] >= '0' && data[position] <= '9') ++position;
            if (position < length && data[position] == '.') {
                size_t fraction = ++position;
                real = 1;
                while (position < length && data[position] >= '0' && data[position] <= '9') ++position;
                if (position == fraction) return dom_error(error, "Invalid JSON fraction");
            }
            if (position < length && (data[position] == 'e' || data[position] == 'E')) {
                size_t exponent;
                real = 1; ++position;
                if (position < length && (data[position] == '+' || data[position] == '-')) ++position;
                exponent = position;
                while (position < length && data[position] >= '0' && data[position] <= '9') ++position;
                if (position == exponent) return dom_error(error, "Invalid JSON exponent");
            }
            if (position < length && !dom_space((unsigned char)data[position]) && data[position] != ',' && data[position] != ']' && data[position] != '}')
                return dom_error(error, "Invalid JSON number separator");
            token_length = position - start;
            if (token_length >= JSON_STRING_SIZE) return dom_error(error, "Argument value too long");
            if (!real && number_policy != DOM_NUM_RAW) {
                const char *maximum = number_policy == DOM_NUM_DOUBLE ? "9007199254740992" :
                    negative ? "9223372036854775808" :
                    number_policy == DOM_NUM_SIGNED ? "9223372036854775807" : "18446744073709551615";
                size_t integer_length = position - digits, max_length = strlen(maximum);
                if (integer_length > max_length || (integer_length == max_length && memcmp(data + digits, maximum, max_length) > 0))
                    return dom_error(error, "JSON integer is outside the native DOM's lossless range");
            }
            if (real && number_policy != DOM_NUM_RAW) {
                char number[JSON_STRING_SIZE], *end;
                double value;
                memcpy(number, data + start, token_length); number[token_length] = '\0';
                errno = 0; value = strtod(number, &end);
                if (*end != '\0' || !isfinite(value) || (errno == ERANGE && value == 0.0))
                    return dom_error(error, "JSON real is outside the native DOM's finite range");
            }
            if (++arguments > settings->max_args) return dom_error(error, "Argument number limit exceeded");
            if (dom_remember_number(numbers, data + start, token_length,
                                      real ? DOM_REAL : DOM_INTEGER, error) != 0) return 2;
            continue;
        }
        if (c == 't' || c == 'f' || c == 'n') {
            const char *literal = c == 't' ? "true" : c == 'f' ? "false" : "null";
            size_t count = strlen(literal);
            if (length - position < count || memcmp(data + position, literal, count) != 0) return dom_error(error, "Invalid JSON literal");
            position += count;
            if (++arguments > settings->max_args) return dom_error(error, "Argument number limit exceeded");
            continue;
        }
        return dom_error(error, "Unexpected character in JSON input");
    }
    return 0;
}

static inline int dom_preflight(const char *data, size_t length, const dom_settings *settings,
                                 int nul_policy, int number_policy, size_t *keys, char **error) {
    dom_locale_guard locale = {0};
    int result;
    if (number_policy == DOM_NUM_RAW)
        return dom_preflight_impl(data, length, settings, nul_policy, number_policy, keys, NULL, error);
    result = dom_locale_begin(&locale, error);
    if (result != 0) return result;
    result = dom_preflight_impl(data, length, settings, nul_policy, number_policy, keys, NULL, error);
    return dom_locale_finish(&locale, result, error);
}

/* Borrow numeric tokens from the still-live input. Only native parse success
 * makes these eligible for output; source-order native traversal checks their
 * number kind and total count, including during silent validation. Native
 * range checks remain in effect, even though output avoids binary reformatting. */
static inline int dom_preflight_numbers(const char *data, size_t length,
                                         const dom_settings *settings, int nul_policy,
                                         int number_policy, size_t *keys,
                                         dom_numbers *numbers, char **error) {
    dom_locale_guard locale = {0};
    int result;
    if (number_policy == DOM_NUM_RAW)
        return dom_preflight_impl(data, length, settings, nul_policy, number_policy, keys, numbers, error);
    result = dom_locale_begin(&locale, error);
    if (result != 0) return result;
    result = dom_preflight_impl(data, length, settings, nul_policy, number_policy, keys, numbers, error);
    return dom_locale_finish(&locale, result, error);
}

enum { DOM_SCALAR = 0, DOM_OBJECT = 1, DOM_ARRAY = 2 };
/* Large/aligned enough for native cursor structs; adapters use memcpy. */
typedef union { void *pointer; size_t index; void *slots[8]; long double alignment; } dom_cursor;
typedef struct { const char *data; size_t length; char number[128]; int number_kind; } dom_value;
typedef struct {
    int (*kind)(const void *node);
    int (*child)(const void *node, dom_cursor *cursor, int first,
                  const void **child, const char **key, size_t *key_length);
    int (*scalar)(const void *node, dom_value *value, char **error);
} dom_adapter;

typedef struct {
    const void *node;
    dom_cursor cursor;
    int first;
    size_t prefix_before, key_length;
    char *key_before;
} dom_frame;

static int dom_walk_impl(const void *root, const dom_adapter *adapter,
                          const dom_settings *settings, size_t *object_keys,
                          const dom_numbers *numbers, char **error) {
    dom_frame *frames = NULL;
    size_t depth = 0, capacity = 0, prefix_length = 0, key_length = 0;
    unsigned int arguments = 0;
    size_t number_position = 0;
    char prefix[JSON_STRING_SIZE], key[JSON_STRING_SIZE];
    const void *node = root;
    int result = 0, entering = 1;
    *object_keys = 0;
    for (;;) {
        if (entering) {
            int kind = adapter->kind(node);
            if (kind != DOM_SCALAR) {
                dom_frame *frame;
                size_t new_length = kind == DOM_ARRAY && depth == 0 ? 5 :
                    prefix_length + (prefix_length ? 1 : 0) + key_length;
                if (depth >= settings->max_depth) { result = dom_error(error, "JSON depth limit exceeded"); break; }
                if (new_length >= JSON_STRING_SIZE) { result = dom_error(error, "Argument name too long"); break; }
                if (depth == capacity) {
                    dom_frame *grown;
                    size_t next = capacity ? capacity * 2 : 16;
                    if (next < capacity || next > SIZE_MAX / sizeof(*frames)) { result = dom_error(error, "DOM traversal stack overflow"); break; }
                    grown = (dom_frame *)realloc(frames, next * sizeof(*frames));
                    if (grown == NULL) { result = dom_error(error, "Unable to allocate DOM traversal stack"); break; }
                    frames = grown; capacity = next;
                }
                frame = &frames[depth];
                memset(frame, 0, sizeof(*frame));
                frame->node = node; frame->first = 1;
                frame->prefix_before = prefix_length; frame->key_length = key_length;
                frame->key_before = (char *)malloc(key_length + 1);
                if (frame->key_before == NULL) { result = dom_error(error, "Unable to allocate DOM traversal key"); break; }
                memcpy(frame->key_before, key, key_length);
                if (kind == DOM_ARRAY && depth == 0) {
                    memcpy(prefix, "array", 5); memcpy(key, "array", 5);
                    prefix_length = key_length = 5;
                } else {
                    if (prefix_length) prefix[prefix_length++] = '.';
                    memcpy(prefix + prefix_length, key, key_length);
                    prefix_length += key_length;
                }
                ++depth;
            } else {
                dom_value value;
                size_t name_length = prefix_length + (prefix_length ? 1 : 0) + key_length;
                memset(&value, 0, sizeof(value));
                if (arguments >= settings->max_args) { result = dom_error(error, "Argument number limit exceeded"); break; }
                if (name_length >= JSON_STRING_SIZE) { result = dom_error(error, "Argument name too long"); break; }
                result = adapter->scalar(node, &value, error);
                if (result != 0) break;
                if (numbers != NULL && value.number_kind != DOM_NOT_NUMBER) {
                    const dom_number_span *span;
                    if (number_position >= numbers->count) {
                        result = dom_error(error, "Native DOM has unexpected numeric arguments"); break;
                    }
                    span = &numbers->spans[number_position++];
                    if (value.number_kind != DOM_ANY_NUMBER && value.number_kind != span->kind) {
                        result = dom_error(error, "Native DOM numeric type differs from source token"); break;
                    }
                    value.data = span->data; value.length = span->length;
                }
                if (value.length >= JSON_STRING_SIZE) { result = dom_error(error, "Argument value too long"); break; }
                ++arguments;
                if (!settings->silence) {
                    int ok = 1;
                    if (prefix_length) {
                        ok = fwrite(prefix, 1, prefix_length, stdout) == prefix_length;
                        ok = (fputc('.', stdout) != EOF) && ok;
                    }
                    ok = (fwrite(key, 1, key_length, stdout) == key_length) && ok;
                    ok = (fwrite(": ", 1, 2, stdout) == 2) && ok;
                    if (value.length) ok = (fwrite(value.data, 1, value.length, stdout) == value.length) && ok;
                    ok = (fputc('\n', stdout) != EOF) && ok;
                    if (!ok) { result = dom_error(error, "Unable to write JSON argument output"); break; }
                }
            }
        }
        entering = 0;
        while (depth) {
            dom_frame *frame = &frames[depth - 1];
            const char *child_key = NULL;
            size_t child_key_length = 0;
            if (adapter->child(frame->node, &frame->cursor, frame->first,
                                &node, &child_key, &child_key_length)) {
                frame->first = 0;
                if (adapter->kind(frame->node) == DOM_OBJECT) {
                    ++*object_keys;
                    if (child_key_length >= JSON_STRING_SIZE) { result = dom_error(error, "Argument name too long"); goto done; }
                    memcpy(key, child_key, child_key_length); key_length = child_key_length;
                }
                entering = 1;
                break;
            }
            prefix_length = frame->prefix_before;
            key_length = frame->key_length;
            memcpy(key, frame->key_before, key_length);
            free(frame->key_before);
            --depth;
        }
        if (!entering) break;
    }
done:
    while (depth) free(frames[--depth].key_before);
    free(frames);
    if (result == 0 && numbers != NULL && number_position != numbers->count)
        result = dom_error(error, "Native DOM omitted numeric arguments from source");
    if (result == 0 && !settings->silence && fflush(stdout) != 0)
        result = dom_error(error, "Unable to flush JSON argument output");
    return result;
}

static inline int dom_walk(const void *root, const dom_adapter *adapter,
                            const dom_settings *settings, size_t *object_keys, char **error) {
    return dom_walk_impl(root, adapter, settings, object_keys, NULL, error);
}

static inline int dom_walk_numbers(const void *root, const dom_adapter *adapter,
                                    const dom_settings *settings, size_t *object_keys,
                                    const dom_numbers *numbers, char **error) {
    return dom_walk_impl(root, adapter, settings, object_keys, numbers, error);
}

#endif
