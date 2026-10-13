#include "yajlparser.h"

#if HAVE_YAJL

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct yajl_path_frame {
    size_t prefix_length;
    size_t key_length;
    unsigned char *key;
};

static int reject(yajl_json_data *p, const char *message) {
    p->callback_error = message;
    return 0;
}

static int json_add_argument(yajl_json_data *p, const unsigned char *value, size_t length) {
    if (p->current_arg_num >= p->arg_num_limit) {
        p->arg_num_limit_exceeded = 1;
        return reject(p, "Argument number limit exceeded");
    }
    size_t name_length = p->prefix_len + (p->prefix_len ? 1 : 0) + p->current_key_len;
    if (name_length >= JSON_STRING_SIZE) return reject(p, "Argument name too long");
    if (length >= JSON_STRING_SIZE) return reject(p, "Argument value too long");
    ++p->current_arg_num;
    if (p->silence) return 1;
    int ok = 1;
    if (p->prefix_len) {
        ok = fwrite(p->prefix, 1, p->prefix_len, stdout) == p->prefix_len;
        ok = (fputc('.', stdout) != EOF) && ok;
    }
    ok = (fwrite(p->current_key, 1, p->current_key_len, stdout) == p->current_key_len) && ok;
    ok = (fwrite(": ", 1, 2, stdout) == 2) && ok;
    ok = (fwrite(value, 1, length, stdout) == length) && ok;
    ok = (fputc('\n', stdout) != EOF) && ok;
    return ok ? 1 : reject(p, "Unable to write JSON argument output");
}

static int map_key(void *ctx, const unsigned char *key, size_t length) {
    yajl_json_data *p = ctx;
    if (length >= JSON_STRING_SIZE) return reject(p, "Argument name too long");
    memcpy(p->current_key, key, length);
    p->current_key[length] = '\0';
    p->current_key_len = length;
    return 1;
}
static int null_value(void *ctx) { return json_add_argument(ctx, (const unsigned char *)"", 0); }
static int boolean_value(void *ctx, int value) {
    return json_add_argument(ctx, (const unsigned char *)(value ? "true" : "false"), value ? 4 : 5);
}
static int string_value(void *ctx, const unsigned char *value, size_t length) {
    return json_add_argument(ctx, value, length);
}
static int number_value(void *ctx, const char *value, size_t length) {
    return json_add_argument(ctx, (const unsigned char *)value, length);
}

static int start_container(yajl_json_data *p, int array) {
    if (p->current_depth >= p->depth_limit) {
        p->depth_limit_exceeded = 1;
        return reject(p, "JSON depth limit exceeded");
    }
    int root_array = array && p->frame_count == 0;
    size_t length = 5;
    if (!root_array)
        length = p->prefix_len + (p->prefix_len ? 1 : 0) + p->current_key_len;
    if (length >= JSON_STRING_SIZE) return reject(p, "Argument name too long");
    if (p->frame_count == p->frame_capacity) {
        if (p->frame_capacity > SIZE_MAX / (2 * sizeof(yajl_path_frame)))
            return reject(p, "JSON path allocation limit exceeded");
        size_t capacity = p->frame_capacity ? p->frame_capacity * 2 : 16;
        yajl_path_frame *frames = realloc(p->frames, capacity * sizeof(*frames));
        if (!frames) return reject(p, "Unable to allocate JSON path stack");
        p->frames = frames;
        p->frame_capacity = capacity;
    }
    unsigned char *saved = malloc(p->current_key_len + 1);
    if (!saved) return reject(p, "Unable to allocate JSON path key");
    memcpy(saved, p->current_key, p->current_key_len);
    saved[p->current_key_len] = '\0';
    p->frames[p->frame_count++] = (yajl_path_frame){p->prefix_len, p->current_key_len, saved};
    if (root_array) {
        memcpy(p->prefix, "array", 5);
        memcpy(p->current_key, "array", 5);
        p->prefix_len = 5;
        p->current_key_len = 5;
    } else {
        if (p->prefix_len) p->prefix[p->prefix_len++] = '.';
        memcpy(p->prefix + p->prefix_len, p->current_key, p->current_key_len);
        p->prefix_len += p->current_key_len;
    }
    p->prefix[p->prefix_len] = '\0';
    p->current_key[p->current_key_len] = '\0';
    ++p->current_depth;
    return 1;
}
static int start_object(void *ctx) { return start_container(ctx, 0); }
static int start_array(void *ctx) { return start_container(ctx, 1); }
static int end_container(void *ctx) {
    yajl_json_data *p = ctx;
    if (!p->frame_count) return reject(p, "Unpaired JSON container event");
    yajl_path_frame *frame = &p->frames[--p->frame_count];
    p->prefix_len = frame->prefix_length;
    p->prefix[p->prefix_len] = '\0';
    p->current_key_len = frame->key_length;
    memcpy(p->current_key, frame->key, frame->key_length + 1);
    free(frame->key);
    --p->current_depth;
    return 1;
}
static const yajl_callbacks callbacks = {
    null_value, boolean_value, NULL, NULL, number_value, string_value,
    start_object, map_key, end_container, start_array, end_container
};

static void clear_state(yajl_json_data *p) {
    for (size_t i = 0; i < p->frame_count; ++i) free(p->frames[i].key);
    p->frame_count = 0;
    p->prefix_len = 0;
    p->current_key_len = 0;
    p->prefix[0] = '\0';
    p->current_key[0] = '\0';
    p->current_depth = 0;
    p->current_arg_num = 0;
    p->depth_limit_exceeded = 0;
    p->arg_num_limit_exceeded = 0;
    p->callback_error = NULL;
    p->escape_state = 0;
    p->escape_digits = 0;
    p->escape_codepoint = 0;
    p->low_surrogate_required = 0;
    p->number_token = 0;
    p->completed = 0;
    p->token_length = 0;
    p->utf8_remaining = 0;
    p->utf8_min = 0x80;
    p->utf8_max = 0xbf;
    p->status = yajl_status_ok;
}
static int reset(yajl_json_data *p, char **error_msg) {
    if (p->handle) yajl_free(p->handle);
    clear_state(p);
    p->handle = yajl_alloc(&callbacks, NULL, p);
    if (!p->handle) {
        *error_msg = strdup("Unable to allocate YAJL parser");
        return -1;
    }
    yajl_config(p->handle, yajl_allow_partial_values, 0);
    yajl_config(p->handle, yajl_dont_validate_strings, 0);
    return 1;
}
int yajl_json_init(yajl_json_data **json, char **error_msg) {
    if (!error_msg) return -1;
    *error_msg = NULL;
    if (!json) return -1;
    *json = NULL;
    yajl_json_data *p = calloc(1, sizeof(*p));
    if (!p) { *error_msg = strdup("Unable to allocate YAJL parser"); return -1; }
    p->prefix = malloc(JSON_STRING_SIZE);
    p->current_key = malloc(JSON_STRING_SIZE);
    p->depth_limit = 500;
    p->arg_num_limit = 500;
    if (!p->prefix || !p->current_key || reset(p, error_msg) < 0) {
        if (!*error_msg) *error_msg = strdup("Unable to allocate YAJL path buffers");
        yajl_json_cleanup(p);
        return -1;
    }
    *json = p;
    return 1;
}

// Narrow incremental token/Unicode guard. YAJL 2.1 does not validate that an
// escaped surrogate pair's second unit is a low surrogate. It also grows lexer
// buffers before callbacks can reject oversized values. JSON grammar stays YAJL's.
static int check_utf8_byte(yajl_json_data *p, unsigned char c) {
    // YAJL 2.1's native check accepts overlong encodings and surrogate
    // codepoints; enforce Unicode scalar UTF-8 independently across chunks.
    if (p->utf8_remaining) {
        if (c < p->utf8_min || c > p->utf8_max)
            return reject(p, "Invalid UTF-8 sequence");
        --p->utf8_remaining;
        p->utf8_min = 0x80;
        p->utf8_max = 0xbf;
        return 1;
    }
    if (c < 0x80) return 1;
    p->utf8_min = 0x80;
    p->utf8_max = 0xbf;
    if (c >= 0xc2 && c <= 0xdf) {
        p->utf8_remaining = 1;
        return 1;
    }
    if (c >= 0xe0 && c <= 0xef) {
        p->utf8_remaining = 2;
        if (c == 0xe0) p->utf8_min = 0xa0;
        if (c == 0xed) p->utf8_max = 0x9f;
        return 1;
    }
    if (c >= 0xf0 && c <= 0xf4) {
        p->utf8_remaining = 3;
        if (c == 0xf0) p->utf8_min = 0x90;
        if (c == 0xf4) p->utf8_max = 0x8f;
        return 1;
    }
    return reject(p, "Invalid UTF-8 sequence");
}

static void start_unicode_escape(yajl_json_data *p) {
    p->escape_state = 3;
    p->escape_digits = 0;
    p->escape_codepoint = 0;
}

static int finish_unicode_escape(yajl_json_data *p) {
    if (p->low_surrogate_required) {
        if (p->escape_codepoint < 0xdc00 || p->escape_codepoint > 0xdfff)
            return reject(p, "Invalid Unicode surrogate escape");
        p->low_surrogate_required = 0;
        p->escape_state = 1;
        return 1;
    }
    if (p->escape_codepoint >= 0xd800 && p->escape_codepoint <= 0xdbff) {
        p->low_surrogate_required = 1;
        p->escape_state = 4;
        return 1;
    }
    if (p->escape_codepoint >= 0xdc00 && p->escape_codepoint <= 0xdfff)
        return reject(p, "Invalid Unicode surrogate escape");
    p->escape_state = 1;
    return 1;
}

static int check_unicode_digit(yajl_json_data *p, unsigned char c) {
    unsigned int digit;
    if (c >= '0' && c <= '9') digit = c - '0';
    else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
    else return reject(p, "Invalid Unicode escape");
    p->escape_codepoint = p->escape_codepoint * 16 + digit;
    ++p->escape_digits;
    if (p->escape_digits < 4) return 1;
    return finish_unicode_escape(p);
}

static int check_escape_byte(yajl_json_data *p, unsigned char c) {
    switch (p->escape_state) {
    case 1:
        if (c == '"') {
            p->escape_state = 0;
            p->token_length = 0;
        } else if (c == '\\') p->escape_state = 2;
        break;
    case 2:
        if (c == 'u') start_unicode_escape(p);
        else p->escape_state = 1;
        break;
    case 3:
        return check_unicode_digit(p, c);
    case 4:
        if (c != '\\') return reject(p, "Invalid Unicode surrogate escape");
        p->escape_state = 5;
        break;
    case 5:
        if (c != 'u') return reject(p, "Invalid Unicode surrogate escape");
        start_unicode_escape(p);
        break;
    }
    return 1;
}

static int check_token_byte(yajl_json_data *p, unsigned char c, size_t budget) {
    if (p->escape_state) {
        ++p->token_length;
        if (p->token_length > budget) return reject(p, "JSON token allocation limit exceeded");
        return check_escape_byte(p, c);
    }
    if (c == '"') {
        p->number_token = 0;
        p->escape_state = 1;
        p->token_length = 1;
        return 1;
    }
    int number_char = (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E';
    if (p->number_token && number_char) {
        ++p->token_length;
        if (p->token_length > budget) return reject(p, "JSON token allocation limit exceeded");
    } else {
        p->number_token = (c >= '0' && c <= '9') || c == '-';
        p->token_length = p->number_token ? 1 : 0;
    }
    return 1;
}

static int check_input(yajl_json_data *p, const char *buf, size_t length) {
    const size_t budget = (size_t)JSON_STRING_SIZE * 8 + 16;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)buf[i];
        if (!c) return reject(p, "Invalid raw NUL byte in JSON input");
        if (!check_utf8_byte(p, c) || !check_token_byte(p, c, budget)) return 0;
    }
    return 1;
}
static int status_result(yajl_json_data *p, const char *buf, size_t length, char **error_msg) {
    if (p->callback_error) *error_msg = strdup(p->callback_error);
    else if (p->status != yajl_status_ok) {
        unsigned char *message = yajl_get_error(p->handle, 0, (const unsigned char *)buf, length);
        *error_msg = strdup(message ? (const char *)message : "YAJL parsing failed");
        if (message) yajl_free_error(p->handle, message);
    } else return 1;
    return -1;
}
int yajl_json_process_chunk(yajl_json_data *p, const char *buf, unsigned int length, char **error_msg) {
    if (!error_msg) return -1;
    *error_msg = NULL;
    if (!p || !p->handle || (!buf && length)) return -1;
    if (p->completed) { *error_msg = strdup("YAJL document is already finalized"); return -1; }
    if (p->callback_error || !check_input(p, buf, length)) return status_result(p, buf, length, error_msg);
    p->status = yajl_parse(p->handle, (const unsigned char *)(buf ? buf : ""), length);
    return status_result(p, buf, length, error_msg);
}
int yajl_json_process(yajl_json_data *p, const char *buf, unsigned int length, char **error_msg) {
    return yajl_json_process_chunk(p, buf, length, error_msg);
}
int yajl_json_complete(yajl_json_data *p, char **error_msg) {
    if (!error_msg) return -1;
    *error_msg = NULL;
    if (!p || !p->handle) return -1;
    if (p->completed) { *error_msg = strdup("YAJL document is already finalized"); return -1; }
    if (p->callback_error) return status_result(p, "", 0, error_msg);
    if (p->utf8_remaining) {
        reject(p, "Truncated UTF-8 sequence");
        return status_result(p, "", 0, error_msg);
    }
    p->status = yajl_complete_parse(p->handle);
    p->completed = 1;
    if (status_result(p, "", 0, error_msg) < 0) return -1;
    if (!p->silence && fflush(stdout) != 0) {
        *error_msg = strdup("Unable to write JSON argument output");
        return -1;
    }
    return 1;
}
int yajl_json_cleanup(yajl_json_data *p) {
    if (!p) return 1;
    if (p->handle) yajl_free(p->handle);
    for (size_t i = 0; i < p->frame_count; ++i) free(p->frames[i].key);
    free(p->frames);
    free(p->prefix);
    free(p->current_key);
    free(p);
    return 1;
}
int yajl_parse_file(yajl_json_data *p, const char *filename, char **error_msg) {
    if (!error_msg) return -1;
    *error_msg = NULL;
    if (!p || !filename) return -1;
    if (p->depth_limit < 0 || p->arg_num_limit < 0) {
        *error_msg = strdup("Invalid negative YAJL parser limit");
        return -1;
    }
    FILE *input = fopen(filename, "rb");
    if (!input) { *error_msg = strdup("Unable to open JSON file"); return -1; }
    if (reset(p, error_msg) < 0) { fclose(input); return -1; }
    char buffer[16384];
    size_t length;
    int rc = 1;
    while ((length = fread(buffer, 1, sizeof(buffer), input)) != 0) {
        rc = yajl_json_process_chunk(p, buffer, (unsigned int)length, error_msg);
        if (rc < 0) break;
    }
    if (rc > 0 && ferror(input)) { *error_msg = strdup("Error reading JSON file"); rc = -1; }
    fclose(input);
    if (rc > 0) rc = yajl_json_complete(p, error_msg);
    return rc > 0 ? 0 : -1;
}

#endif
