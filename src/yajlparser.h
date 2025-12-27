#ifndef YAJLPARSER_H
#define YAJLPARSER_H

#include "../config.h"

#if HAVE_YAJL

typedef struct yajl_json_data yajl_json_data;

#include <yajl/yajl_parse.h>

struct yajl_json_data {
    /* yajl configuration and parser state */
    yajl_handle handle;
    yajl_status status;

    /* prefix is used to create data hierarchy (i.e., 'parent.child.value') */
    // Note that cppcheck-suppress needed to avoid false positive for unused struct members
    unsigned char *prefix;                   // cppcheck-suppress unusedStructMember
    size_t         prefix_len;               // cppcheck-suppress unusedStructMember
    unsigned char *current_key;              // cppcheck-suppress unusedStructMember
    size_t         current_key_len;          // cppcheck-suppress unusedStructMember
    long int       current_depth;            // cppcheck-suppress unusedStructMember
    long int       depth_limit;              // cppcheck-suppress unusedStructMember
    int            depth_limit_exceeded;     // cppcheck-suppress unusedStructMember
    long int       current_arg_num;          // cppcheck-suppress unusedStructMember
    long int       arg_num_limit;            // cppcheck-suppress unusedStructMember
    int            arg_num_limit_exceeded;   // cppcheck-suppress unusedStructMember
    int            silence;                  // cppcheck-suppress unusedStructMember
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
