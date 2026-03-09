#include "rjparser.h"

#include <iostream>
#include <cstdio>
#include <vector>
#include <deque>
#include <string>

#ifdef HAVE_RAPIDJSON

#include "pathhandler.h"

#include "rapidjson/reader.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/error/en.h"

class RJSAXHandler {
  public:
    explicit RJSAXHandler():
    m_max_depth(0),
    m_current_depth(0),
    m_depth_limit_exceeded(false),
    m_arg_num_counter(0),
    m_arg_num_limit(0),
    m_arg_num_limit_exceeded(false),
    m_silence(false),
    m_path()
    {};
    ~RJSAXHandler() {};

    static bool init() {
        return true;
    }

    bool addArgument(const std::string& value) {

        m_arg_num_counter++;
        if (m_arg_num_limit > 0 &&
            m_arg_num_counter > m_arg_num_limit) {
            m_arg_num_limit_exceeded = true;
            std::cerr << "Argument number limit exceeded" << std::endl;
            return false;
        }

        if (m_silence) {
            return true;
        }
        /*
         * Argument name is 'm_prefix + m_current_key'
         */
        char argname[JSON_STRING_SIZE];
        m_path.buildArgName(argname);
        if (m_path.m_error) {
            std::cerr << "Argument name too long" << std::endl;
            return false;
        }
        if (value.size() >= JSON_STRING_SIZE) {
            std::cerr << "Argument value too long" << std::endl;
            return false;
        }
        std::cout << argname << ": " << value << std::endl;
        return true;
    }

    /* RapidJSON mandatory methods */
    bool StartObject() {  // cppcheck-suppress unusedFunction
        m_path.push();
        if (m_path.m_error) {
            std::cerr << "Argument name too long" << std::endl;
            return false;
        }
        m_current_depth++;
        if (m_current_depth > m_max_depth) {
            m_depth_limit_exceeded = true;
            return false;
        }
        return true;
    }

    bool EndObject(rapidjson::SizeType) {   // cppcheck-suppress unusedFunction
        m_path.pop();
        m_current_depth--;
        return true;
    }

    bool StartArray() {  // cppcheck-suppress unusedFunction
        m_path.startArraySpecial();
        if (m_path.m_error) {
            std::cerr << "Argument name too long" << std::endl;
            return false;
        }
        m_current_depth++;
        if (m_current_depth > m_max_depth) {
            m_depth_limit_exceeded = true;
            return false;
        }
        return true;
    }

    bool EndArray(rapidjson::SizeType) {   // cppcheck-suppress unusedFunction
        m_path.pop();
        m_current_depth--;
        return true;
    }

    bool Key(const char* k, size_t l, bool) {  // cppcheck-suppress unusedFunction
        m_path.setKey(k, l);
        if (m_path.m_error) {
            std::cerr << "Argument name too long" << std::endl;
            return false;
        }
        return true;
    }

    bool String(const char* v, size_t l, bool b) {  // cppcheck-suppress unusedFunction
        std::string val = std::string(reinterpret_cast<const char*>(v), l);
        return addArgument(val);
    }

    bool Int(int64_t v) {  // cppcheck-suppress unusedFunction
        std::string val = std::to_string(v);
        return addArgument(val);
    }

    bool Uint(uint64_t v) {  // cppcheck-suppress unusedFunction
        std::string val = std::to_string(v);
        return addArgument(val);
    }

    bool Double(double v) {  // cppcheck-suppress unusedFunction
        std::string val = std::to_string(v);
        return addArgument(val);
    }

    bool Bool(bool b) {  // cppcheck-suppress unusedFunction
        if (b) {
            addArgument("true");
        } else {
            addArgument("false");
        }
        return true;
    }

    bool Null() {  // cppcheck-suppress unusedFunction
        addArgument("");
        return true;
    }

    bool RawNumber(const char* str, rapidjson::SizeType length, bool) {  // cppcheck-suppress unusedFunction
        std::string val = std::string(reinterpret_cast<const char*>(str), length);
        return addArgument(val);
    }

    bool Int64(int64_t i) {  // cppcheck-suppress unusedFunction
        std::string val = std::string(reinterpret_cast<const char*>(i));
        return addArgument(val);
    }

    bool Uint64(uint64_t u) {  // cppcheck-suppress unusedFunction
        std::string val = std::string(reinterpret_cast<const char*>(u));
        return addArgument(val);
    }

    /* RapidJSON mandatory methods END */

    void setMaxDepth(double max_depth) {
        m_max_depth = max_depth;
    }

    void setMaxArgNum(double max_arg_num) {
        m_arg_num_limit = static_cast<long int>(max_arg_num);
    }

    void setSilence(int silence) {
        if (silence) {
            m_silence = true;
        } else {
            m_silence = false;
        }
    }

    private:
    double         m_max_depth;
    int64_t        m_current_depth;
    bool           m_depth_limit_exceeded;
    long int       m_arg_num_counter;
    long int       m_arg_num_limit;
    bool           m_arg_num_limit_exceeded;
    bool           m_silence;
    PathHandler    m_path;

};

struct rj_parser {
    RJSAXHandler impl;
};

extern "C" int rj_json_init(rj_parser **parser, char **error_msg) {
    assert(error_msg != NULL);
    assert(parser != NULL);

    try {
        *parser = new rj_parser{};
    }
    catch (const std::exception &e) {
        *parser = nullptr;
        *error_msg = strdup(e.what());
        return 1;
    }

    *error_msg = NULL;
    (*parser)->impl.init();
    return 0;
}

/**
 * Frees the resources used for JSON parsing.
 */
extern "C" int rj_json_cleanup(rj_parser *parser) {
    delete(parser);
    return 0;
}

extern "C" int rj_parse_buffer(rj_parser *parser, const char *buf, unsigned int len,
                              char **error_msg) {
    if (!buf || !error_msg) return -1;

    std::vector<char> tmp(buf, buf + len);
    tmp.push_back('\0');

    rapidjson::StringStream ss(tmp.data());
    rapidjson::Reader reader;

    rapidjson::ParseResult pr = reader.Parse(ss, (parser)->impl);
    if (!pr) {
        fprintf(stderr, "RapidJSON error: %s at %zu\n",
                rapidjson::GetParseError_En(pr.Code()), pr.Offset());
        return 2;
    }
    return 0;
}

extern "C" int rj_set_max_depth(rj_parser *parser, double max_depth) {
    parser->impl.setMaxDepth(max_depth);
    return 0;
}

extern "C" int rj_set_max_arg_num(rj_parser *parser, double max_arg_num) {
    parser->impl.setMaxArgNum(max_arg_num);
    return 0;
}

extern "C" int rj_set_silence(rj_parser *parser, int silence) {
    parser->impl.setSilence(silence);
    return 0;
}

extern "C" int rj_parser_cleanup(rj_parser *parser) {
    parser->impl.~RJSAXHandler();
    return 0;
}

#endif