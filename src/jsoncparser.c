#include "jsoncparser.h"
#if HAVE_JSONC
#include "domarguments.h"
#include <json-c/json.h>
#include <json-c/linkhash.h>

struct jsonc_parser { dom_settings settings; };

static int jsonc_kind(const void *node) {
    enum json_type type = json_object_get_type((struct json_object *)node);
    return type == json_type_object ? DOM_OBJECT : type == json_type_array ? DOM_ARRAY : DOM_SCALAR;
}

static int jsonc_child(const void *node, dom_cursor *cursor, int first,
                       const void **child, const char **key, size_t *key_length) {
    struct json_object *object = (struct json_object *)node;
    if (jsonc_kind(node) == DOM_ARRAY) {
        if (first) cursor->index = 0;
        if (cursor->index >= json_object_array_length(object)) return 0;
        *child = json_object_array_get_idx(object, cursor->index++);
    } else {
        struct lh_entry *entry = first ? lh_table_head(json_object_get_object(object)) :
            lh_entry_next((struct lh_entry *)cursor->pointer);
        if (entry == NULL) return 0;
        cursor->pointer = entry;
        *key = (const char *)lh_entry_k(entry); *key_length = strlen(*key);
        *child = lh_entry_v(entry);
    }
    return 1;
}

static int jsonc_scalar(const void *node, dom_value *value, char **error) {
    struct json_object *object = (struct json_object *)node;
    switch (json_object_get_type(object)) {
        case json_type_null: value->data = ""; value->length = 0; break;
        case json_type_boolean:
            value->data = json_object_get_boolean(object) ? "true" : "false";
            value->length = strlen(value->data); break;
        case json_type_string:
            value->data = json_object_get_string(object);
            value->length = (size_t)json_object_get_string_len(object); break;
        case json_type_int:
        case json_type_double:
            /* json-c retains the original serialized text for parsed doubles.
             * Integer range is checked before its saturating conversion. */
            value->data = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
            if (value->data == NULL) return dom_error(error, "Unable to serialize JSON-C number");
            value->length = strlen(value->data); break;
        default: return dom_error(error, "Unexpected JSON-C scalar type");
    }
    if (json_object_get_type(object) == json_type_int) value->number_kind = DOM_INTEGER;
    else if (json_object_get_type(object) == json_type_double) value->number_kind = DOM_REAL;
    return 0;
}

static const dom_adapter jsonc_adapter = {jsonc_kind, jsonc_child, jsonc_scalar};

static void jsonc_unicode_escape(char *output, size_t *used, unsigned int code) {
    static const char hex[] = "0123456789abcdef";
    output[(*used)++] = '\\'; output[(*used)++] = 'u';
    output[(*used)++] = hex[(code >> 12) & 15];
    output[(*used)++] = hex[(code >> 8) & 15];
    output[(*used)++] = hex[(code >> 4) & 15];
    output[(*used)++] = hex[code & 15];
}

/* JSON-C 0.18's strict tokener rejects raw non-ASCII UTF-8 on this platform.
 * Preserve strict native grammar checking by converting already verified
 * scalar UTF-8 to equivalent JSON Unicode escapes. This is a representation
 * conversion only; escaped characters and structural bytes are unchanged. */
static int jsonc_ascii_input(const char *data, size_t length, char **ascii,
                              size_t *ascii_length, char **error) {
    size_t position = 0, used = 0, output_length = length;
    char *output;
    while (position < length) {
        unsigned char first = (unsigned char)data[position];
        size_t start = position, encoded;
        if (first < 0x80) { ++position; continue; }
        /* The preflight above guarantees successful scalar decoding. */
        if (!dom_utf8(data, length, &position)) return dom_error(error, "Invalid JSON-C Unicode input");
        encoded = first < 0xf0 ? 6 : 12;
        if (output_length > SIZE_MAX - encoded - 1) return dom_error(error, "JSON-C input size overflow");
        output_length += encoded - (position - start);
    }
    if (output_length == length) { *ascii = NULL; *ascii_length = length; return 0; }
    output = (char *)malloc(output_length + 1);
    if (output == NULL) return dom_error(error, "Unable to allocate JSON-C Unicode input");
    position = 0;
    while (position < length) {
        unsigned int code = (unsigned char)data[position++];
        size_t count, i;
        if (code < 0x80) { output[used++] = (char)code; continue; }
        if (code < 0xe0) { code &= 31; count = 1; }
        else if (code < 0xf0) { code &= 15; count = 2; }
        else { code &= 7; count = 3; }
        for (i = 0; i < count; ++i) code = code * 64 + ((unsigned char)data[position++] & 63);
        if (code <= 0xffff) {
            jsonc_unicode_escape(output, &used, code);
        } else {
            unsigned int offset = code - 0x10000;
            jsonc_unicode_escape(output, &used, 0xd800 + (offset >> 10));
            jsonc_unicode_escape(output, &used, 0xdc00 + (offset & 1023));
        }
    }
    output[used] = '\0';
    *ascii = output; *ascii_length = used;
    return 0;
}

int jsonc_parse_file(jsonc_parser *parser, const char *filename, char **error_msg) {
    char *content = NULL, *ascii = NULL;
    const char *input;
    size_t length = 0, keys = 0, native_keys = 0, end;
    dom_numbers numbers = {0};
    dom_locale_guard locale = {0};
    struct json_tokener *tokener = NULL;
    struct json_object *root = NULL;
    dom_settings check;
    int rc;
    if (error_msg == NULL) return -1;
    *error_msg = NULL;
    if (parser == NULL || filename == NULL) return dom_error(error_msg, "Invalid JSON-C parser arguments");
    rc = dom_locale_begin(&locale, error_msg);
    if (rc != 0) return rc;
    rc = dom_read_file(filename, &content, &length, error_msg);
    if (rc != 0) goto done;
    rc = dom_preflight_numbers(content, length, &parser->settings, DOM_NUL_KEY_FORBIDDEN,
                                DOM_NUM_INT64_UINT64, &keys, &numbers, error_msg);
    if (rc != 0) goto done;
    rc = jsonc_ascii_input(content, length, &ascii, &length, error_msg);
    if (rc != 0) goto done;
    input = ascii != NULL ? ascii : content;
    if (length >= INT_MAX) {
        rc = dom_error(error_msg, "JSON-C input exceeds native API range"); goto done;
    }
    /* The native tokener counts scalar slots as well as container frames.
     * Bound native state allocation even when a caller sets UINT_MAX. Limits
     * above 8192 do not remove this adapter's independent native stack cap. */
    tokener = json_tokener_new_ex((int)(parser->settings.max_depth < JSON_STRING_SIZE ?
                                      parser->settings.max_depth : JSON_STRING_SIZE) + 1);
    if (tokener == NULL) { rc = dom_error(error_msg, "Unable to allocate JSON-C tokener"); goto done; }
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    /* Include our sentinel so a root number at physical EOF is finalized. Raw
     * NUL inside the actual input was rejected by the lexical boundary guard. */
    root = json_tokener_parse_ex(tokener, input, (int)length + 1);
    if (json_tokener_get_error(tokener) != json_tokener_success) {
        rc = dom_error(error_msg, json_tokener_error_desc(json_tokener_get_error(tokener))); goto done;
    }
    end = json_tokener_get_parse_end(tokener);
    while (end < length && dom_space((unsigned char)input[end])) ++end;
    if (end < length) { rc = dom_error(error_msg, "Unexpected content after JSON document"); goto done; }
    /* JSON-C coalesces repeated keys. Check retained vs. input key counts before
     * emitting output; reject rather than silently dropping security arguments. */
    check = parser->settings; check.silence = 1;
    rc = dom_walk(root, &jsonc_adapter, &check, &native_keys, error_msg);
    if (rc != 0) goto done;
    if (native_keys != keys) { rc = dom_error(error_msg, "JSON-C DOM cannot preserve repeated object keys"); goto done; }
    rc = dom_walk_numbers(root, &jsonc_adapter, &parser->settings, &native_keys, &numbers, error_msg);
 done:
    if (root != NULL) json_object_put(root);
    if (tokener != NULL) json_tokener_free(tokener);
    free(numbers.spans);
    free(ascii);
    free(content);
    return dom_locale_finish(&locale, rc, error_msg);
}

int jsonc_json_init(jsonc_parser **parser, char **error_msg) {
    if (error_msg == NULL) return -1;
    *error_msg = NULL;
    if (parser == NULL) return dom_error(error_msg, "Invalid JSON-C parser output pointer");
    *parser = (jsonc_parser *)calloc(1, sizeof(**parser));
    if (*parser == NULL) return dom_error(error_msg, "Unable to allocate JSON-C parser");
    dom_settings_init(&(*parser)->settings);
    return 0;
}

int jsonc_set_max_depth(jsonc_parser *parser, double value) {
    return parser == NULL ? -1 : dom_set_limit(value, &parser->settings.max_depth);
}

int jsonc_set_max_arg_num(jsonc_parser *parser, double value) {
    return parser == NULL ? -1 : dom_set_limit(value, &parser->settings.max_args);
}

int jsonc_set_silence(jsonc_parser *parser, int silence) {
    if (parser == NULL) return -1;
    parser->settings.silence = silence != 0;
    return 0;
}

int jsonc_json_cleanup(jsonc_parser *parser) {
    free(parser);
    return 0;
}

#endif
