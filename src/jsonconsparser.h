#ifndef JSONCONS_PARSER_H
#define JSONCONS_PARSER_H

#include "../config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if HAVE_JSONCONS

typedef struct jsoncons_parser jsoncons_parser;

/* Zero denotes success; nonzero denotes failure. Parse/init errors, when
 * available, are malloc-owned strings that the caller must free. All C entry
 * points contain C++ exceptions. Cleanup accepts NULL; parsing starts fresh
 * on every call while retaining the configured limits and silent setting.
 * Limits are nonnegative integral doubles <= UINT_MAX. A zero limit allows
 * no containers (depth) or no scalar arguments (argument count), respectively.
 * CLI limits must be positive. Root containers have depth 1, root scalars 0.
 * Null is an empty argument value; every scalar counts, including null/bool.
 */
int jsoncons_json_init(jsoncons_parser **parser, char **error_msg);
int jsoncons_parse_file(jsoncons_parser *parser, const char *filename, char **error_msg);
int jsoncons_set_max_depth(jsoncons_parser *parser, double max_depth);
int jsoncons_set_max_arg_num(jsoncons_parser *parser, double max_arg_num);
int jsoncons_set_silence(jsoncons_parser *parser, int silence);
int jsoncons_json_cleanup(jsoncons_parser *parser);

#endif

#ifdef __cplusplus
}
#endif

#endif
