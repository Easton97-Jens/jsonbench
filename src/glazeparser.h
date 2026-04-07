#ifndef GLAZE_PARSER_H
#define GLAZE_PARSER_H

#include "../config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if HAVE_GLAZE

typedef struct glaze_parser glaze_parser;

int glaze_json_init(glaze_parser **parser, char **error_msg);
int glaze_parse_file(glaze_parser *parser, const char *filename, char **error_msg);
int glaze_set_max_depth(glaze_parser *parser, double max_depth);
int glaze_set_max_arg_num(glaze_parser *parser, double max_arg_num);
int glaze_set_silence(glaze_parser *parser, int silence);
int glaze_json_cleanup(glaze_parser *parser);

#endif

#ifdef __cplusplus
}
#endif

#endif
