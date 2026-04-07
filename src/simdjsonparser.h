#ifndef SIMDJSON_PARSER_H
#define SIMDJSON_PARSER_H

#include "../config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if HAVE_SIMDJSON

typedef struct simdjson_parser simdjson_parser;

int simdjson_json_init(simdjson_parser **parser, char **error_msg);
int simdjson_parse_file(simdjson_parser *parser, const char *filename, char **error_msg);
int simdjson_set_max_depth(simdjson_parser *parser, double max_depth);
int simdjson_set_max_arg_num(simdjson_parser *parser, double max_arg_num);
int simdjson_set_silence(simdjson_parser *parser, int silence);
int simdjson_json_cleanup(simdjson_parser *parser);

#endif

#ifdef __cplusplus
}
#endif

#endif
