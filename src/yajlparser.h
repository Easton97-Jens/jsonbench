#ifndef YAJLPARSER_H
#define YAJLPARSER_H

#include "../config.h"
#include "jsonbench.h"

#if HAVE_YAJL

typedef struct yajl_json_data yajl_json_data;

#include <yajl/yajl_parse.h>

struct yajl_json_data {
    /* yajl configuration and parser state */
    yajl_handle handle;
    yajl_status status;

    /* prefix is used to create data hierarchy (i.e., 'parent.child.value') */
    unsigned char *prefix;
    size_t         prefix_len;
    unsigned char *current_key;
    size_t         current_key_len;
    long int       current_depth;
    long int       depth_limit;
    int            depth_limit_exceeded;
    long int       current_arg_num;
    long int       arg_num_limit;
    int            arg_num_limit_exceeded;
    int            silence;
};

int yajl_json_init(yajl_json_data **json, char **error_msg);

int yajl_json_process(yajl_json_data *json, const char *buf,
    unsigned int size, char **error_msg);

int yajl_json_complete(yajl_json_data *json, char **error_msg);

int yajl_json_cleanup(yajl_json_data *json);

int yajl_json_process_chunk(yajl_json_data *json, const char *buf,
        unsigned int size, char **error_msg);

int yajl_json_cleanup(yajl_json_data *json);

#endif

#endif
