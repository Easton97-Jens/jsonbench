#ifndef JSONCONS_PARSER_H
#define JSONCONS_PARSER_H

#include "../config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if HAVE_JSONCONS

typedef struct jsoncons_parser jsoncons_parser;

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
