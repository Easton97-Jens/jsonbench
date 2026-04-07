#ifndef JSONC_PARSER_H
#define JSONC_PARSER_H

#include "../config.h"

#if HAVE_JSONC

typedef struct jsonc_parser jsonc_parser;

int jsonc_json_init(jsonc_parser **parser, char **error_msg);
int jsonc_parse_file(jsonc_parser *parser, const char *filename, char **error_msg);
int jsonc_set_max_depth(jsonc_parser *parser, double max_depth);
int jsonc_set_max_arg_num(jsonc_parser *parser, double max_arg_num);
int jsonc_set_silence(jsonc_parser *parser, int silence);
int jsonc_json_cleanup(jsonc_parser *parser);

#endif

#endif
