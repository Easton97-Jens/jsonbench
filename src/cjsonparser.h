#ifndef CJSON_PARSER_H
#define CJSON_PARSER_H

#include "../config.h"

#if HAVE_CJSON

typedef struct cjson_parser cjson_parser;

int cjson_json_init(cjson_parser **parser, char **error_msg);
int cjson_parse_file(cjson_parser *parser, const char *filename, char **error_msg);
int cjson_set_max_depth(cjson_parser *parser, double max_depth);
int cjson_set_max_arg_num(cjson_parser *parser, double max_arg_num);
int cjson_set_silence(cjson_parser *parser, int silence);
int cjson_json_cleanup(cjson_parser *parser);

#endif

#endif
