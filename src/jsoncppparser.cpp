#include "jsoncppparser.h"

#if HAVE_JSONCPP

#include <fstream>
#include <json/json.h>
#include <new>
#include <string>
#include <cstring>

struct jsoncpp_parser {
    int silence;
};

extern "C" int jsoncpp_json_init(jsoncpp_parser **parser, char **error_msg) {
    if (parser == nullptr || error_msg == nullptr) {
        return -1;
    }

    try {
        *parser = new jsoncpp_parser{};
    } catch (const std::bad_alloc&) {
        *parser = nullptr;
        *error_msg = strdup("Unable to allocate JsonCpp parser");
        return 1;
    }

    *error_msg = nullptr;
    return 0;
}

extern "C" int jsoncpp_parse_file(jsoncpp_parser *parser, const char *filename, char **error_msg) {
    (void)parser;
    if (filename == nullptr || error_msg == nullptr) {
        return -1;
    }

    std::ifstream input(filename);
    if (!input.is_open()) {
        *error_msg = strdup("Unable to open JSON file");
        return 2;
    }

    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errs;
    const bool ok = Json::parseFromStream(builder, input, &root, &errs);
    if (!ok) {
        *error_msg = strdup(errs.c_str());
        return 2;
    }

    return 0;
}

extern "C" int jsoncpp_set_max_depth(jsoncpp_parser *parser, double max_depth) {
    (void)parser;
    (void)max_depth;
    return 0;
}

extern "C" int jsoncpp_set_max_arg_num(jsoncpp_parser *parser, double max_arg_num) {
    (void)parser;
    (void)max_arg_num;
    return 0;
}

extern "C" int jsoncpp_set_silence(jsoncpp_parser *parser, int silence) {
    if (parser != nullptr) {
        parser->silence = silence;
    }
    return 0;
}

extern "C" int jsoncpp_json_cleanup(jsoncpp_parser *parser) {
    delete parser;
    return 0;
}

#endif
