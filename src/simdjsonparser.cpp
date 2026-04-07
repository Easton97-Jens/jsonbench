#include "simdjsonparser.h"

#if HAVE_SIMDJSON

#include <simdjson.h>
#include <cstring>
#include <new>

struct simdjson_parser {
    int silence;
    simdjson::ondemand::parser parser;
};

static int consume_value(simdjson::ondemand::value value) {
    simdjson::ondemand::json_type type;
    auto type_result = value.type();
    if (type_result.error()) {
        return 1;
    }
    type = type_result.value_unsafe();

    switch (type) {
        case simdjson::ondemand::json_type::object: {
            auto obj_result = value.get_object();
            if (obj_result.error()) {
                return 1;
            }
            auto obj = obj_result.value_unsafe();
            for (auto field_result : obj) {
                if (field_result.error()) {
                    return 1;
                }
                auto field = field_result.value_unsafe();
                auto key_result = field.unescaped_key(false);
                if (key_result.error()) {
                    return 1;
                }
                auto child = field.value();
                int rc = consume_value(child);
                if (rc != 0) {
                    return rc;
                }
            }
            return 0;
        }
        case simdjson::ondemand::json_type::array: {
            auto arr_result = value.get_array();
            if (arr_result.error()) {
                return 1;
            }
            auto arr = arr_result.value_unsafe();
            for (auto elem_result : arr) {
                if (elem_result.error()) {
                    return 1;
                }
                auto elem = elem_result.value_unsafe();
                int rc = consume_value(elem);
                if (rc != 0) {
                    return rc;
                }
            }
            return 0;
        }
        case simdjson::ondemand::json_type::number: {
            auto number_result = value.get_number();
            return number_result.error() ? 1 : 0;
        }
        case simdjson::ondemand::json_type::string: {
            auto str_result = value.get_string();
            return str_result.error() ? 1 : 0;
        }
        case simdjson::ondemand::json_type::boolean: {
            auto bool_result = value.get_bool();
            return bool_result.error() ? 1 : 0;
        }
        case simdjson::ondemand::json_type::null: {
            auto null_result = value.is_null();
            return null_result.error() ? 1 : 0;
        }
    }

    return 1;
}

extern "C" int simdjson_json_init(simdjson_parser **parser, char **error_msg) {
    if (parser == nullptr || error_msg == nullptr) {
        return -1;
    }

    try {
        *parser = new simdjson_parser{};
    } catch (const std::bad_alloc&) {
        *parser = nullptr;
        *error_msg = strdup("Unable to allocate simdjson parser");
        return 1;
    }

    *error_msg = nullptr;
    return 0;
}

extern "C" int simdjson_parse_file(simdjson_parser *parser, const char *filename, char **error_msg) {
    if (parser == nullptr || filename == nullptr || error_msg == nullptr) {
        return -1;
    }

    auto json_result = simdjson::padded_string::load(filename);
    if (json_result.error()) {
        *error_msg = strdup(simdjson::error_message(json_result.error()));
        return 2;
    }

    auto &json = json_result.value_unsafe();
    auto doc_result = parser->parser.iterate(json);
    if (doc_result.error()) {
        *error_msg = strdup(simdjson::error_message(doc_result.error()));
        return 2;
    }

    auto &doc = doc_result.value_unsafe();
    int rc = consume_value(doc);
    if (rc != 0) {
        *error_msg = strdup("simdjson parse/iteration failed");
        return 2;
    }

    return 0;
}

extern "C" int simdjson_set_max_depth(simdjson_parser *parser, double max_depth) {
    (void)parser;
    (void)max_depth;
    return 0;
}

extern "C" int simdjson_set_max_arg_num(simdjson_parser *parser, double max_arg_num) {
    (void)parser;
    (void)max_arg_num;
    return 0;
}

extern "C" int simdjson_set_silence(simdjson_parser *parser, int silence) {
    if (parser != nullptr) {
        parser->silence = silence;
    }
    return 0;
}

extern "C" int simdjson_json_cleanup(simdjson_parser *parser) {
    delete parser;
    return 0;
}

#endif
