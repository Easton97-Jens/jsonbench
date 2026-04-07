#ifndef JANSSON_PARSER_H
#define JANSSON_PARSER_H

#include "../config.h"

#if HAVE_JANSSON

typedef struct jansson_parser jansson_parser;

int jansson_json_init(jansson_parser **parser, char **error_msg);
int jansson_parse_file(jansson_parser *parser, const char *filename, char **error_msg);
int jansson_set_max_depth(jansson_parser *parser, double max_depth);
int jansson_set_max_arg_num(jansson_parser *parser, double max_arg_num);
int jansson_set_silence(jansson_parser *parser, int silence);
int jansson_json_cleanup(jansson_parser *parser);

#endif

#endif
