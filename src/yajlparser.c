#include "yajlparser.h"

#if HAVE_YAJL

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const char *base_offset=NULL;

int json_add_argument(yajl_json_data *p, const unsigned char *value, unsigned length)
{
    assert(p != NULL);

    if (p->silence) {
        return 1;
    }

    char argname[JSON_STRING_SIZE];
    char argval[JSON_STRING_SIZE];

    /**
     * Argument name is 'prefix + current_key'
     */
    if (p->prefix_len > 0) {
        if (p->prefix_len + 1 + p->current_key_len >= JSON_STRING_SIZE) {
            printf("Argument name too long\n");
            return 0;
        }
        memcpy(argname, p->prefix, p->prefix_len);
        memcpy(argname + p->prefix_len, ".", 1);
        memcpy(argname + p->prefix_len + 1, p->current_key, p->current_key_len);
        argname[p->prefix_len + 1 + p->current_key_len] = '\0';
    }
    else {
        if (p->current_key_len >= JSON_STRING_SIZE) {
            printf("Argument name too long\n");
            return 0;
        }
        memcpy(argname, p->current_key, p->current_key_len);
        argname[p->current_key_len] = '\0';
    }
    if (length >= JSON_STRING_SIZE) {
        printf("Argument value too long\n");
        return 0;
    }
    memcpy(argval, value, length);
    argval[length] = '\0';
    printf("%s: %s\n", argname, argval);
    p->current_arg_num++;
    if (p->current_arg_num > p->arg_num_limit) {
        p->arg_num_limit_exceeded = 1;
        return 0;
    }

    return 1;
}

/**
 * yajl callback functions
 * For more information on the function signatures and order, check
 * http://lloyd.github.com/yajl/yajl-1.0.12/structyajl__callbacks.html
 */

/**
 * Callback for hash key values; we use those to define the variable names
 * under ARGS. Whenever we reach a new key, we update the current key value.
 */
static int yajl_map_key(void *ctx, const unsigned char *key, size_t length)
{
    yajl_json_data *p = (yajl_json_data *) ctx;
    assert(p != NULL);
    unsigned char *safe_key = (unsigned char *) NULL;

    /**
     * yajl does not provide us with null-terminated strings, but
     * rather expects us to copy the data from the key up to the
     * length informed; we create a standalone null-termined copy
     * in safe_key
     */
    safe_key = (unsigned char *)strndup((char *)key, length);

    (unsigned char*)strcpy((char *)p->current_key, (char *)safe_key);
    p->current_key[length] = '\0';
    p->current_key_len = length;
    free(safe_key);
    return 1;
}

static int yajl_null(void *ctx)
{
    yajl_json_data *p = (yajl_json_data *) ctx;
    assert(p != NULL);

    return json_add_argument(p, (unsigned char*)"", 0);
}

static int yajl_boolean(void *ctx, int value)
{
    yajl_json_data *p = (yajl_json_data *) ctx;
    assert(p != NULL);

    if (value) {
        return json_add_argument(p,  (unsigned char*)"true", strlen("true"));
    }
    else {
        return json_add_argument(p,  (unsigned char*)"false", strlen("false"));
    }
}

static int yajl_string(void *ctx, const unsigned char *value, size_t length)
{
    yajl_json_data *p = (yajl_json_data *) ctx;
    assert(p != NULL);

    return json_add_argument(p, value, length);
}

static int yajl_number(void *ctx, const char *value, size_t length)
{
    yajl_json_data *p = (yajl_json_data *) ctx;
    assert(p != NULL);

    return json_add_argument(p, (unsigned char*)value, length);
}

static int yajl_start_array(void *ctx) {
    yajl_json_data *p = (yajl_json_data *) ctx;
    assert(p != NULL);

    if (p->prefix_len == 0 && p->current_key_len == 0) {
        memcpy(p->prefix, "array", 5);
        p->prefix_len = 5;
        p->prefix[p->prefix_len] = '\0';
        memcpy(p->current_key, "array", 5);
        p->current_key_len = 5;
        p->current_key[p->current_key_len] = '\0';
    }
    else if (p->prefix_len > 0) {
        memcpy(p->prefix + p->prefix_len, ".", 1);
        p->prefix_len++;
        memcpy(p->prefix + p->prefix_len, p->current_key, p->current_key_len);
        p->prefix_len += p->current_key_len;
        p->prefix[p->prefix_len] = '\0';
    }
    else {
        memcpy(p->prefix, p->current_key, p->current_key_len);
        p->prefix_len += p->current_key_len;
        p->prefix[p->prefix_len] = '\0';
    }

    p->current_depth++;
    if (p->current_depth > p->depth_limit) {
        p->depth_limit_exceeded = 1;
        return 0;
    }

    return 1;
}


static int yajl_end_array(void *ctx) {
    yajl_json_data *p = (yajl_json_data *) ctx;
    assert(p != NULL);

    size_t separator = p->prefix_len;

    for(int i = p->prefix_len - 1; i >= 0; i--) {
        if (p->prefix[i] == '.') {
            separator = i;
            break;
        }
    }

    if (separator < p->prefix_len) {
        p->prefix[separator] = '\0';
        p->prefix_len = separator;
    }
    else {
        p->prefix[0] = '\0';
        p->prefix_len = 0;
    }
    p->current_depth--;

    return 1;
}

/**
 * Callback for a new hash, which indicates a new subtree, labeled as the current
 * argument name, is being created
 */
static int yajl_start_map(void *ctx)
{
    yajl_json_data *p = (yajl_json_data *) ctx;
    assert(p != NULL);

    if (p->prefix_len == 0) {
        memcpy(p->prefix, p->current_key, p->current_key_len);
        p->prefix_len = p->current_key_len;
        p->prefix[p->prefix_len] = '\0';
    }
    else {
        memcpy(p->prefix + p->prefix_len, ".", 1);
        p->prefix_len++;
        memcpy(p->prefix + p->prefix_len, p->current_key, p->current_key_len);
        p->prefix_len += p->current_key_len;
        p->prefix[p->prefix_len] = '\0';
    }
    p->current_depth++;
    if (p->current_depth > p->depth_limit) {
        p->depth_limit_exceeded = 1;
        return 0;
    }

    return 1;
}

/**
 * Callback for end hash, meaning the current subtree is being closed, and that
 * we should go back to the parent variable label
 */
static int yajl_end_map(void *ctx)
{
    yajl_json_data *p = (yajl_json_data *) ctx;
    assert(p != NULL);
    size_t separator = p->prefix_len;

    for(int i = p->prefix_len - 1; i >= 0; i--) {
        if (p->prefix[i] == '.') {
            separator = i;
            break;
        }
    }

    if (separator < p->prefix_len) {
        memcpy(p->current_key, p->prefix + separator + 1, p->prefix_len - separator - 1);
        p->current_key_len = p->prefix_len - separator - 1;
        p->prefix[separator] = '\0';
        p->prefix_len = separator;
    }
    else {
        memcpy(p->current_key, p->prefix, p->prefix_len);
        p->current_key_len = p->prefix_len;
        p->prefix[0] = '\0';
        p->prefix_len = 0;
    }
    p->current_depth--;

    return 1;
}

/**
 * Initialise JSON parser.
 */
int yajl_json_init(yajl_json_data **json, char **error_msg) {
    assert(error_msg != NULL);
    /**
     * yajl configuration and callbacks
     */
    static yajl_callbacks callbacks = {
        yajl_null,
        yajl_boolean,
        NULL /* yajl_integer  */,
        NULL /* yajl_double */,
        yajl_number,
        yajl_string,
        yajl_start_map,
        yajl_map_key,
        yajl_end_map,
        yajl_start_array,
        yajl_end_array
    };

    *error_msg = NULL;

    (*json) = malloc(sizeof(yajl_json_data));
    if (*json == NULL) return -1;

    /**
     * Prefix and current key are initially empty
     */
    (*json)->prefix = (unsigned char *) malloc(JSON_STRING_SIZE * sizeof(unsigned char));
    if ((*json)->prefix == NULL) return -1;
    memset((*json)->prefix, 0, JSON_STRING_SIZE);
    (*json)->prefix_len = 0;
    (*json)->current_key = (unsigned char *) malloc(JSON_STRING_SIZE * sizeof(unsigned char));
    if ((*json)->current_key == NULL) return -1;
    memset((*json)->current_key, 0, JSON_STRING_SIZE);
    (*json)->current_key_len = 0;

    (*json)->current_depth          = 0;
    (*json)->depth_limit            = 0;
    (*json)->depth_limit_exceeded   = 0;
    (*json)->current_arg_num        = 0;
    (*json)->arg_num_limit          = 0;
    (*json)->arg_num_limit_exceeded = 0;
    (*json)->silence                = 0;

    /**
     * yajl initialization
     *
     * yajl_parser_config definition:
     * http://lloyd.github.io/yajl/yajl-2.0.1/yajl__parse_8h.html#aec816c5518264d2ac41c05469a0f986c
     *
     * TODO: make UTF8 validation optional, as it depends on Content-Encoding
     */

    (*json)->handle = yajl_alloc(&callbacks, NULL, (*json));
    yajl_config((*json)->handle, yajl_allow_partial_values, 0);

    return 1;
}

/**
 * Feed one chunk of data to the JSON parser.
 */
int yajl_json_process_chunk(yajl_json_data *json, const char *buf, unsigned int size, char **error_msg) {
    assert(json != NULL);
    assert(error_msg != NULL);
    *error_msg = NULL;
    base_offset=buf;

    /* Feed our parser and catch any errors */
    json->status = yajl_parse(json->handle, (unsigned char *)buf, size);

    if (json->status != yajl_status_ok) {
        if (json->depth_limit_exceeded != 0 || json->arg_num_limit_exceeded != 0) {
            if (json->depth_limit_exceeded != 0) {
                const char * err = "JSON depth limit exceeded";
                *error_msg = strdup(err);
            }
            if (json->arg_num_limit_exceeded != 0) {
                const char * err = "ARGUMENT number limit exceeded";
                if (*error_msg == NULL) {
                    *error_msg = strdup(err);
                }
                else {
                    *error_msg = realloc(*error_msg, strlen(*error_msg) + strlen(err) + 1);
                    strcat(*error_msg, err);
                }
            }
        }
        else {
            unsigned char* yajl_err = yajl_get_error(json->handle, 0, (unsigned char *)buf, size);
            *error_msg = strdup((char *)yajl_err);
            yajl_free_error(json->handle, yajl_err);
        }
        return -1;
    }

    return 1;
}

/**
 * Frees the resources used for JSON parsing.
 */
int yajl_json_cleanup(yajl_json_data *json) {
    assert(json != NULL);
    if (json->handle != NULL) {
        yajl_free(json->handle);
        json->handle = NULL;
    }
    free(json->prefix);
    free(json->current_key);
    free(json);

    return 1;
}

#endif
