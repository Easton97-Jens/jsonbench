#include "cjsonparser.h"

#if HAVE_CJSON

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

struct cjson_parser {
    int silence;
};

static int read_whole_file(const char *filename, char **content, long *length, char **error_msg) {
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        *error_msg = strdup("Unable to open JSON file");
        return 1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        *error_msg = strdup("Unable to seek JSON file");
        return 1;
    }

    long len = ftell(fp);
    if (len < 0) {
        fclose(fp);
        *error_msg = strdup("Unable to measure JSON file");
        return 1;
    }
    rewind(fp);

    char *buffer = (char *)malloc((size_t)len + 1U);
    if (buffer == NULL) {
        fclose(fp);
        *error_msg = strdup("Unable to allocate JSON buffer");
        return 1;
    }

    size_t read_len = fread(buffer, 1, (size_t)len, fp);
    fclose(fp);
    if (read_len != (size_t)len) {
        free(buffer);
        *error_msg = strdup("Unable to read entire JSON file");
        return 1;
    }

    buffer[len] = '\0';
    *content = buffer;
    *length = len;
    return 0;
}

int cjson_json_init(cjson_parser **parser, char **error_msg) {
    assert(parser != NULL);
    assert(error_msg != NULL);

    *parser = (cjson_parser *)calloc(1, sizeof(cjson_parser));
    if (*parser == NULL) {
        *error_msg = strdup("Unable to allocate cJSON parser");
        return 1;
    }

    *error_msg = NULL;
    return 0;
}

int cjson_parse_file(cjson_parser *parser, const char *filename, char **error_msg) {
    (void)parser;
    if (filename == NULL || error_msg == NULL) {
        return -1;
    }

    char *content = NULL;
    long length = 0;
    int rc = read_whole_file(filename, &content, &length, error_msg);
    if (rc != 0) {
        return 2;
    }

    cJSON *root = cJSON_Parse(content);
    free(content);

    if (root == NULL) {
        const char *cjson_error = cJSON_GetErrorPtr();
        if (cjson_error != NULL) {
            *error_msg = strdup(cjson_error);
        } else {
            *error_msg = strdup("cJSON_Parse failed");
        }
        return 2;
    }

    cJSON_Delete(root);
    return 0;
}

int cjson_set_max_depth(cjson_parser *parser, double max_depth) {
    (void)parser;
    (void)max_depth;
    return 0;
}

int cjson_set_max_arg_num(cjson_parser *parser, double max_arg_num) {
    (void)parser;
    (void)max_arg_num;
    return 0;
}

int cjson_set_silence(cjson_parser *parser, int silence) {
    if (parser != NULL) {
        parser->silence = silence;
    }
    return 0;
}

int cjson_json_cleanup(cjson_parser *parser) {
    free(parser);
    return 0;
}

#endif
