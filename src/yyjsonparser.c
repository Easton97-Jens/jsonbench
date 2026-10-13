#include "yyjsonparser.h"
#if HAVE_YYJSON
#include "domarguments.h"
#include <yyjson.h>

struct yyjson_parser { dom_settings settings; };

static int yyjson_kind(void *node) {
    if (yyjson_is_obj((yyjson_val *)node)) return DOM_OBJECT;
    if (yyjson_is_arr((yyjson_val *)node)) return DOM_ARRAY;
    return DOM_SCALAR;
}

static int yyjson_child(void *node, dom_cursor *cursor, int first,
                         void **child, const char **key, size_t *key_length) {
    yyjson_val *json = (yyjson_val *)node;
    if (yyjson_is_arr(json)) {
        yyjson_arr_iter iter;
        if (first) iter = yyjson_arr_iter_with(json);
        else memcpy(&iter, cursor, sizeof(iter));
        *child = yyjson_arr_iter_next(&iter);
        if (*child == NULL) return 0;
        memcpy(cursor, &iter, sizeof(iter));
    } else {
        yyjson_obj_iter iter;
        yyjson_val *native_key;
        if (first) iter = yyjson_obj_iter_with(json);
        else memcpy(&iter, cursor, sizeof(iter));
        native_key = yyjson_obj_iter_next(&iter);
        if (native_key == NULL) return 0;
        *child = yyjson_obj_iter_get_val(native_key);
        *key = yyjson_get_str(native_key); *key_length = yyjson_get_len(native_key);
        memcpy(cursor, &iter, sizeof(iter));
    }
    return 1;
}

static int yyjson_scalar(void *node, dom_value *value, char **error) {
    yyjson_val *json = (yyjson_val *)node;
    if (yyjson_is_null(json)) { value->data = ""; value->length = 0; }
    else if (yyjson_is_bool(json)) {
        value->data = yyjson_get_bool(json) ? "true" : "false"; value->length = strlen(value->data);
    } else if (yyjson_is_str(json)) {
        value->data = yyjson_get_str(json); value->length = yyjson_get_len(json);
    } else if (yyjson_is_raw(json)) {
        value->data = yyjson_get_raw(json); value->length = yyjson_get_len(json);
    } else return dom_error(error, "Unexpected non-lossless yyjson scalar type");
    return 0;
}

static const dom_adapter yyjson_adapter = {yyjson_kind, yyjson_child, yyjson_scalar};
static const dom_input_policy yyjson_policy = {DOM_NUL_ALLOWED, DOM_NUM_RAW};

int yyjson_parse_file(yyjson_parser *parser, const char *filename, char **error_msg) {
    char *content = NULL;
    size_t length = 0;
    size_t keys = 0;
    yyjson_read_err native_error;
    yyjson_doc *doc = NULL;
    int rc;
    if (error_msg == NULL) return -1;
    *error_msg = NULL;
    if (parser == NULL || filename == NULL) return dom_error(error_msg, "Invalid yyjson parser arguments");
    rc = dom_read_file(filename, &content, &length, error_msg);
    if (rc != 0) return rc;
    rc = dom_preflight(content, length, &parser->settings, &yyjson_policy, &keys, error_msg);
    if (rc != 0) goto done;
    /* Public native option preserves arbitrary JSON number lexemes and the
     * native object iterator retains repeated keys in original order. */
    doc = yyjson_read_opts(content, length, YYJSON_READ_NUMBER_AS_RAW, NULL, &native_error);
    if (doc == NULL) {
        char message[256];
        snprintf(message, sizeof(message), "yyjson parse error at byte %zu: %s", native_error.pos,
                 native_error.msg != NULL ? native_error.msg : "unknown error");
        rc = dom_error(error_msg, message); goto done;
    }
    rc = dom_walk(yyjson_doc_get_root(doc), &yyjson_adapter, &parser->settings, &keys, error_msg);
 done:
    yyjson_doc_free(doc);
    free(content);
    return rc;
}

int yyjson_json_init(yyjson_parser **parser, char **error_msg) {
    if (error_msg == NULL) return -1;
    *error_msg = NULL;
    if (parser == NULL) return dom_error(error_msg, "Invalid yyjson parser output pointer");
    *parser = (yyjson_parser *)calloc(1, sizeof(**parser));
    if (*parser == NULL) return dom_error(error_msg, "Unable to allocate yyjson parser");
    dom_settings_init(&(*parser)->settings);
    return 0;
}

int yyjson_set_max_depth(yyjson_parser *parser, double value) {
    return parser == NULL ? -1 : dom_set_limit(value, &parser->settings.max_depth);
}

int yyjson_set_max_arg_num(yyjson_parser *parser, double value) {
    return parser == NULL ? -1 : dom_set_limit(value, &parser->settings.max_args);
}

int yyjson_set_silence(yyjson_parser *parser, int silence) {
    if (parser == NULL) return -1;
    parser->settings.silence = silence != 0;
    return 0;
}

int yyjson_json_cleanup(yyjson_parser *parser) {
    free(parser);
    return 0;
}

#endif
