#include "jsonconsparser.h"

#if HAVE_JSONCONS

#include <fstream>
#include <jsoncons/json.hpp>
#include <new>
#include <sstream>
#include <string>
#include <cstring>

struct jsoncons_parser {
    int silence;
};

extern "C" int jsoncons_json_init(jsoncons_parser **parser, char **error_msg) {
    if (parser == nullptr || error_msg == nullptr) {
        return -1;
    }

    try {
        *parser = new jsoncons_parser{};
    } catch (const std::bad_alloc&) {
        *parser = nullptr;
        *error_msg = strdup("Unable to allocate jsoncons parser");
        return 1;
    }

    *error_msg = nullptr;
    return 0;
}

extern "C" int jsoncons_parse_file(jsoncons_parser *parser, const char *filename, char **error_msg) {
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

    try {
        auto parsed = jsoncons::json::parse(oss.str());
        (void)parsed;
    } catch (const std::exception &e) {
        *error_msg = strdup(e.what());
        return 2;
    }

    return 0;
}

extern "C" int jsoncons_set_max_depth(jsoncons_parser *parser, double max_depth) {
    (void)parser;
    (void)max_depth;
    return 0;
}

extern "C" int jsoncons_set_max_arg_num(jsoncons_parser *parser, double max_arg_num) {
    (void)parser;
    (void)max_arg_num;
    return 0;
}

extern "C" int jsoncons_set_silence(jsoncons_parser *parser, int silence) {
    if (parser != nullptr) {
        parser->silence = silence;
    }
    return 0;
}

extern "C" int jsoncons_json_cleanup(jsoncons_parser *parser) {
    delete parser;
    return 0;
}

#endif
