#include "cjsonparser.h"
#if HAVE_CJSON
#include "domarguments.h"
#include <cjson/cJSON.h>

struct cjson_parser { dom_settings settings; };

static int cjson_kind(void *node) {
    if (cJSON_IsObject((cJSON *)node)) return DOM_OBJECT;
    if (cJSON_IsArray((cJSON *)node)) return DOM_ARRAY;
    return DOM_SCALAR;
}

static int cjson_child(void *node, dom_cursor *cursor, int first,
                        void **child, const char **key, size_t *key_length) {
    cJSON *json = (cJSON *)node;
    cJSON *item = first ? json->child : ((cJSON *)cursor->pointer)->next;
    if (item == NULL) return 0;
    cursor->pointer = (void *)item; *child = item;
    if (cJSON_IsObject(json)) {
        *key = item->string; *key_length = strlen(item->string);
    }
    return 1;
}

static int cjson_scalar(void *node, dom_value *value, char **error) {
    cJSON *json = (cJSON *)node;
    if (cJSON_IsNull(json)) { value->data = ""; value->length = 0; }
    else if (cJSON_IsBool(json)) {
        value->data = cJSON_IsTrue(json) ? "true" : "false"; value->length = strlen(value->data);
    } else if (cJSON_IsString(json)) {
        value->data = json->valuestring; value->length = strlen(value->data);
    } else if (cJSON_IsNumber(json)) {
        int length;
        if (!isfinite(json->valuedouble)) return dom_error(error, "Nonfinite cJSON number");
        length = snprintf(value->number, sizeof(value->number), "%.17g", json->valuedouble);
        if (length < 0 || (size_t)length >= sizeof(value->number)) return dom_error(error, "Unable to format cJSON number");
        value->data = value->number; value->length = (size_t)length;
        value->number_kind = DOM_ANY_NUMBER;
    } else return dom_error(error, "Unexpected cJSON scalar type");
    return 0;
}

static const dom_adapter cjson_adapter = {cjson_kind, cjson_child, cjson_scalar};
static const dom_input_policy cjson_policy = {DOM_NUL_FORBIDDEN, DOM_NUM_DOUBLE};

int cjson_parse_file(cjson_parser *parser, const char *filename, char **error_msg) {
    char *content = NULL;
    size_t length = 0;
    size_t keys = 0;
    dom_numbers numbers = {0};
    dom_locale_guard locale = {0};
    const char *end = NULL;
    cJSON *root = NULL;
    int rc;
    rc = dom_begin_numeric_file(parser, filename, "Invalid cJSON parser arguments",
                                  &locale, &content, &length, error_msg);
    if (rc != 0) return rc;
    /* cJSON has no decoded string lengths and stores all numbers as doubles.
     * Reject NUL and unsafe integers instead of silently truncating/rounding. */
    rc = dom_preflight_numbers(content, length, &parser->settings, &cjson_policy,
                                &keys, &numbers, error_msg);
    if (rc != 0) goto done;
    root = cJSON_ParseWithLengthOpts(content, length + 1, &end, 1);
    if (root == NULL) {
        char message[128];
        /* Error pointers refer into content: capture position before freeing. */
        size_t offset = end != NULL && end >= content && end <= content + length ? (size_t)(end - content) : 0;
        snprintf(message, sizeof(message), "cJSON parse error at byte %zu", offset);
        rc = dom_error(error_msg, message); goto done;
    }
    rc = dom_walk_numbers(root, &cjson_adapter, &parser->settings, &keys, &numbers, error_msg);
 done:
    cJSON_Delete(root);
    free(numbers.spans);
    free(content);
    return dom_locale_finish(&locale, rc, error_msg);
}

int cjson_json_init(cjson_parser **parser, char **error_msg) {
    if (error_msg == NULL) return -1;
    *error_msg = NULL;
    if (parser == NULL) return dom_error(error_msg, "Invalid cJSON parser output pointer");
    *parser = (cjson_parser *)calloc(1, sizeof(**parser));
    if (*parser == NULL) return dom_error(error_msg, "Unable to allocate cJSON parser");
    dom_settings_init(&(*parser)->settings);
    return 0;
}

int cjson_set_max_depth(cjson_parser *parser, double value) {
    return parser == NULL ? -1 : dom_set_limit(value, &parser->settings.max_depth);
}

int cjson_set_max_arg_num(cjson_parser *parser, double value) {
    return parser == NULL ? -1 : dom_set_limit(value, &parser->settings.max_args);
}

int cjson_set_silence(cjson_parser *parser, int silence) {
    if (parser == NULL) return -1;
    parser->settings.silence = silence != 0;
    return 0;
}

int cjson_json_cleanup(cjson_parser *parser) {
    free(parser);
    return 0;
}

#endif
