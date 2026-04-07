#include "jsoncparser.h"

#if HAVE_JSONC

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include <json-c/json_util.h>

struct jsonc_parser {
    int silence;
};

int jsonc_json_init(jsonc_parser **parser, char **error_msg) {
    assert(parser != NULL);
    assert(error_msg != NULL);

    *parser = (jsonc_parser *)calloc(1, sizeof(jsonc_parser));
    if (*parser == NULL) {
        *error_msg = strdup("Unable to allocate JSON-C parser");
        return 1;
    }

    *error_msg = NULL;
    return 0;
}

int jsonc_parse_file(jsonc_parser *parser, const char *filename, char **error_msg) {
    (void)parser;
    if (filename == NULL || error_msg == NULL) {
        return -1;
    }

    struct json_object *root = json_object_from_file(filename);
    if (root == NULL) {
        *error_msg = strdup("json_object_from_file failed");
        return 2;
    }

    json_object_put(root);
    return 0;
}

int jsonc_set_max_depth(jsonc_parser *parser, double max_depth) {
    (void)parser;
    (void)max_depth;
    return 0;
}

int jsonc_set_max_arg_num(jsonc_parser *parser, double max_arg_num) {
    (void)parser;
    (void)max_arg_num;
    return 0;
}

int jsonc_set_silence(jsonc_parser *parser, int silence) {
    if (parser != NULL) {
        parser->silence = silence;
    }
    return 0;
}

int jsonc_json_cleanup(jsonc_parser *parser) {
    free(parser);
    return 0;
}

#endif
