#ifndef YYJSON_PARSER_H
#define YYJSON_PARSER_H

#include "../config.h"

#if HAVE_YYJSON

typedef struct yyjson_parser yyjson_parser;

int yyjson_json_init(yyjson_parser **parser, char **error_msg);
int yyjson_parse_file(yyjson_parser *parser, const char *filename, char **error_msg);
int yyjson_set_max_depth(yyjson_parser *parser, double max_depth);
int yyjson_set_max_arg_num(yyjson_parser *parser, double max_arg_num);
int yyjson_set_silence(yyjson_parser *parser, int silence);
int yyjson_json_cleanup(yyjson_parser *parser);

#endif

#endif
