#ifndef NL_PARSER_H
#define NL_PARSER_H

#include "../config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nl_parser nl_parser;

int nl_json_init(nl_parser **parser, char **error_msg);
int nl_parse_buffer(nl_parser *parser, const char *buf, unsigned int len, char **error_msg);
int nl_set_max_depth(nl_parser *parser, double max_depth);
int nl_set_max_arg_num(nl_parser *parser, double max_arg_num);
int nl_set_silence(nl_parser *parser, int silence);
int nl_json_cleanup(nl_parser *parser);

#ifdef __cplusplus
}
#endif

#endif // NL_PARSER_H