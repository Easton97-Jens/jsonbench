#ifndef JSONCPP_PARSER_H
#define JSONCPP_PARSER_H

#include "../config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if HAVE_JSONCPP

typedef struct jsoncpp_parser jsoncpp_parser;

int jsoncpp_json_init(jsoncpp_parser **parser, char **error_msg);
int jsoncpp_parse_file(jsoncpp_parser *parser, const char *filename, char **error_msg);
int jsoncpp_set_max_depth(jsoncpp_parser *parser, double max_depth);
int jsoncpp_set_max_arg_num(jsoncpp_parser *parser, double max_arg_num);
int jsoncpp_set_silence(jsoncpp_parser *parser, int silence);
int jsoncpp_json_cleanup(jsoncpp_parser *parser);

#endif

#ifdef __cplusplus
}
#endif

#endif
