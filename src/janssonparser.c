#include "janssonparser.h"
#if HAVE_JANSSON
#include "domarguments.h"
#include <jansson.h>

struct jansson_parser { dom_settings settings; };

static int jansson_kind(const void *node) {
    return json_is_object((json_t *)node) ? DOM_OBJECT : json_is_array((json_t *)node) ? DOM_ARRAY : DOM_SCALAR;
}

static int jansson_child(const void *node, dom_cursor *cursor, int first,
                          const void **child, const char **key, size_t *key_length) {
    json_t *json = (json_t *)node;
    if (json_is_array(json)) {
        if (first) cursor->index = 0;
        if (cursor->index >= json_array_size(json)) return 0;
        *child = json_array_get(json, cursor->index++);
    } else {
        cursor->pointer = first ? json_object_iter(json) : json_object_iter_next(json, cursor->pointer);
        if (cursor->pointer == NULL) return 0;
        *key = json_object_iter_key(cursor->pointer);
        *key_length = json_object_iter_key_len(cursor->pointer);
        *child = json_object_iter_value(cursor->pointer);
    }
    return 1;
}

static int jansson_scalar(const void *node, dom_value *value, char **error) {
    json_t *json = (json_t *)node;
    int length;
    if (json_is_null(json)) { value->data = ""; value->length = 0; }
    else if (json_is_boolean(json)) {
        value->data = json_is_true(json) ? "true" : "false"; value->length = strlen(value->data);
    } else if (json_is_string(json)) {
        value->data = json_string_value(json); value->length = json_string_length(json);
    } else if (json_is_integer(json)) {
        length = snprintf(value->number, sizeof(value->number), "%" JSON_INTEGER_FORMAT, json_integer_value(json));
        if (length < 0 || (size_t)length >= sizeof(value->number)) return dom_error(error, "Unable to format Jansson integer");
        value->data = value->number; value->length = (size_t)length;
        value->number_kind = DOM_INTEGER;
    } else if (json_is_real(json)) {
        double number = json_real_value(json);
        if (!isfinite(number)) return dom_error(error, "Nonfinite Jansson number");
        length = snprintf(value->number, sizeof(value->number), "%.17g", number);
        if (length < 0 || (size_t)length >= sizeof(value->number)) return dom_error(error, "Unable to format Jansson real");
        value->data = value->number; value->length = (size_t)length;
        value->number_kind = DOM_REAL;
    } else return dom_error(error, "Unexpected Jansson scalar type");
    return 0;
}

static const dom_adapter jansson_adapter = {jansson_kind, jansson_child, jansson_scalar};

int jansson_parse_file(jansson_parser *parser, const char *filename, char **error_msg) {
    char *content = NULL;
    size_t length = 0, keys = 0;
    dom_numbers numbers = {0};
    dom_locale_guard locale = {0};
    json_error_t native_error;
    json_t *root = NULL;
    int rc;
    if (error_msg == NULL) return -1;
    *error_msg = NULL;
    if (parser == NULL || filename == NULL) return dom_error(error_msg, "Invalid Jansson parser arguments");
    rc = dom_locale_begin(&locale, error_msg);
    if (rc != 0) return rc;
    rc = dom_read_file(filename, &content, &length, error_msg);
    if (rc != 0) goto done;
    rc = dom_preflight_numbers(content, length, &parser->settings, DOM_NUL_ALLOWED,
                                DOM_NUM_SIGNED, &keys, &numbers, error_msg);
    if (rc != 0) goto done;
    /* Native objects coalesce keys; fail explicitly instead of silently losing
     * occurrences. JSON_ALLOW_NUL preserves length-aware decoded strings. */
    root = json_loadb(content, length, JSON_DECODE_ANY | JSON_ALLOW_NUL | JSON_REJECT_DUPLICATES, &native_error);
    if (root == NULL) { rc = dom_error(error_msg, native_error.text); goto done; }
    rc = dom_walk_numbers(root, &jansson_adapter, &parser->settings, &keys, &numbers, error_msg);
 done:
    if (root != NULL) json_decref(root);
    free(numbers.spans);
    free(content);
    return dom_locale_finish(&locale, rc, error_msg);
}

int jansson_json_init(jansson_parser **parser, char **error_msg) {
    if (error_msg == NULL) return -1;
    *error_msg = NULL;
    if (parser == NULL) return dom_error(error_msg, "Invalid Jansson parser output pointer");
    *parser = (jansson_parser *)calloc(1, sizeof(**parser));
    if (*parser == NULL) return dom_error(error_msg, "Unable to allocate Jansson parser");
    dom_settings_init(&(*parser)->settings);
    return 0;
}

int jansson_set_max_depth(jansson_parser *parser, double value) {
    return parser == NULL ? -1 : dom_set_limit(value, &parser->settings.max_depth);
}

int jansson_set_max_arg_num(jansson_parser *parser, double value) {
    return parser == NULL ? -1 : dom_set_limit(value, &parser->settings.max_args);
}

int jansson_set_silence(jansson_parser *parser, int silence) {
    if (parser == NULL) return -1;
    parser->settings.silence = silence != 0;
    return 0;
}

int jansson_json_cleanup(jansson_parser *parser) {
    free(parser);
    return 0;
}

#endif
