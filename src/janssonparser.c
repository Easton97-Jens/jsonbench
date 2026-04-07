#include "janssonparser.h"

#if HAVE_JANSSON

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>

struct jansson_parser {
    int silence;
};

int jansson_json_init(jansson_parser **parser, char **error_msg) {
    assert(parser != NULL);
    assert(error_msg != NULL);

    *parser = (jansson_parser *)calloc(1, sizeof(jansson_parser));
    if (*parser == NULL) {
        *error_msg = strdup("Unable to allocate Jansson parser");
        return 1;
    }

    *error_msg = NULL;
    return 0;
}

int jansson_parse_file(jansson_parser *parser, const char *filename, char **error_msg) {
    (void)parser;
    if (filename == NULL || error_msg == NULL) {
        return -1;
    }

    json_error_t jerror;
    json_t *root = json_load_file(filename, 0, &jerror);
    if (root == NULL) {
        *error_msg = strdup(jerror.text);
        return 2;
    }

    json_decref(root);
    return 0;
}

int jansson_set_max_depth(jansson_parser *parser, double max_depth) {
    (void)parser;
    (void)max_depth;
    return 0;
}

int jansson_set_max_arg_num(jansson_parser *parser, double max_arg_num) {
    (void)parser;
    (void)max_arg_num;
    return 0;
}

int jansson_set_silence(jansson_parser *parser, int silence) {
    if (parser != NULL) {
        parser->silence = silence;
    }
    return 0;
}

int jansson_json_cleanup(jansson_parser *parser) {
    free(parser);
    return 0;
}

#endif
