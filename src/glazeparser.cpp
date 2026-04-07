#include "glazeparser.h"

#if HAVE_GLAZE

#include <glaze/glaze.hpp>
#include <cstring>
#include <fstream>
#include <new>
#include <sstream>
#include <string>

struct glaze_parser {
    int silence;
};

extern "C" int glaze_json_init(glaze_parser **parser, char **error_msg) {
    if (parser == nullptr || error_msg == nullptr) {
        return -1;
    }

    try {
        *parser = new glaze_parser{};
    } catch (const std::bad_alloc&) {
        *parser = nullptr;
        *error_msg = strdup("Unable to allocate glaze parser");
        return 1;
    }

    *error_msg = nullptr;
    return 0;
}

extern "C" int glaze_parse_file(glaze_parser *parser, const char *filename, char **error_msg) {
    (void)parser;
    if (filename == nullptr || error_msg == nullptr) {
        return -1;
    }

    std::ifstream input(filename);
    if (!input.is_open()) {
        *error_msg = strdup("Unable to open JSON file");
        return 2;
    }

    std::ostringstream oss;
    oss << input.rdbuf();
    std::string content = oss.str();

    glz::json_t doc;
    const auto ec = glz::read_json(doc, content);
    if (ec) {
        *error_msg = strdup("glz::read_json failed");
        return 2;
    }

    return 0;
}

extern "C" int glaze_set_max_depth(glaze_parser *parser, double max_depth) {
    (void)parser;
    (void)max_depth;
    return 0;
}

extern "C" int glaze_set_max_arg_num(glaze_parser *parser, double max_arg_num) {
    (void)parser;
    (void)max_arg_num;
    return 0;
}

extern "C" int glaze_set_silence(glaze_parser *parser, int silence) {
    if (parser != nullptr) {
        parser->silence = silence;
    }
    return 0;
}

extern "C" int glaze_json_cleanup(glaze_parser *parser) {
    delete parser;
    return 0;
}

#endif
