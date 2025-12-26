#include "rjparser.h"

#include <iostream>
#include <cstdio>
#include <vector>
#include <deque>
#include <string>

#ifdef HAVE_RAPIDJSON

#include "rapidjson/reader.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/error/en.h"

class RJSAXHandler {
    void *ctx;  // contect from C
public:
    explicit RJSAXHandler():
    m_current_depth(0),
    m_depth_limit_exceeded(false),
    m_current_key(JSON_STRING_SIZE, '\0'),
    m_current_key_len(0),
    m_prefix(JSON_STRING_SIZE, '\0'),
    m_prefix_len(0),
    m_arg_num_counter(0),
    m_arg_num_limit(0),
    m_arg_num_limit_exceeded(false),
    m_silence(false)
    {};
    ~RJSAXHandler() {};

    static bool init() {
        return true;
    }

    bool addArgument(const std::string& value) {
        /*
        if (m_silence) {
            return true;
        }
        */

        std::string argname(JSON_STRING_SIZE, '\0');
        std::string argval(JSON_STRING_SIZE, '\0');
        /*
         * Argument name is 'm_prefix + m_current_key'
         */
        if(m_prefix_len > 0) {
            if (m_prefix_len + 1 + m_current_key_len >= JSON_STRING_SIZE) {
                std::cerr << "Argument name too long" << std::endl;
                return false;
            }
            argname.replace(0, m_prefix_len, m_prefix.substr(0, m_prefix_len));
            argname.replace(m_prefix_len, 1, ".");
            argname.replace(m_prefix_len + 1, m_current_key_len,
                m_current_key.substr(0, m_current_key_len));
            argname.resize(m_prefix_len + 1 + m_current_key_len);
        }
        else {
            if (m_current_key_len >= JSON_STRING_SIZE) {
                std::cerr << "Argument name too long" << std::endl;
                return false;
            }
            argname.replace(0, m_current_key_len,
                m_current_key.substr(0, m_current_key_len));
            argname.resize(m_current_key_len);
        }
        if (value.size() >= JSON_STRING_SIZE) {
            std::cerr << "Argument value too long" << std::endl;
            return false;
        }
        argval.replace(0, value.size(), value);
        argval.resize(value.size());
        if (!m_silence) {
            std::cout << argname << ": " << argval << std::endl;
        }
        m_arg_num_counter++;
        if (m_arg_num_limit > 0 &&
            m_arg_num_counter > m_arg_num_limit) {
            m_arg_num_limit_exceeded = true;
            std::cerr << "Argument number limit exceeded" << std::endl;
            return false;
        }
        return true;
    }

    /* RapidJSON mandatory methods */
    bool StartObject() {
        if (m_prefix_len == 0) {
            m_prefix = m_current_key;
            m_prefix_len = m_current_key_len;
        }
        else {
            m_prefix.replace(m_prefix_len, 1, ".");
            m_prefix_len += 1;
            m_prefix.replace(m_prefix_len, m_current_key_len, m_current_key);
            m_prefix_len += m_current_key_len;
            m_prefix[m_prefix_len] = '\0';
        }
        m_current_depth++;
        if (m_current_depth > m_max_depth) {
            m_depth_limit_exceeded = true;
            return false;
        }
        return true;
    }

    bool EndObject(rapidjson::SizeType)   {

        size_t separator = static_cast<size_t>(m_prefix_len);
        for(int i = static_cast<int>(m_prefix_len) - 1; i >= 0; i--) {
            if (m_prefix[i] == '.') {
                separator = static_cast<size_t>(i);
                break;
            }
        }
        if (separator < m_prefix_len) {
            m_current_key.replace(0, m_prefix_len - separator - 1,
                m_prefix.substr(separator + 1, m_prefix_len - separator - 1));
            m_current_key_len = m_prefix_len - separator - 1;
            m_prefix[separator] = '\0';
            m_prefix_len = separator;
        }
        else {
            m_current_key.replace(0, m_prefix_len, m_prefix.substr(0, m_prefix_len));
            m_current_key_len = m_prefix_len;
            m_prefix[0] = '\0';
            m_prefix_len = 0;
        }
        m_current_depth--;

        return true;
    }

    bool StartArray()  {
        if (m_prefix_len == 0 && m_current_key_len == 0) {
            m_prefix = "array";
            m_prefix_len = 5;
            m_prefix[m_prefix_len] = '\0';
            m_current_key = "array";
            m_current_key_len = 5;
            m_current_key[m_current_key_len] = '\0';
        }
        else if (m_prefix_len > 0) {
            m_prefix.replace(m_prefix_len, 1, ".");
            m_prefix_len += 1;
            m_prefix.replace(m_prefix_len, m_current_key_len, m_current_key);
            m_prefix_len += m_current_key_len;
            m_prefix[m_prefix_len] = '\0';
        }
        else {
            m_prefix.replace(0, m_current_key_len, m_current_key);
            m_prefix_len += m_current_key_len;
            m_prefix[m_prefix_len] = '\0';
        }
        m_current_depth++;
        if (m_current_depth > m_max_depth) {
            m_depth_limit_exceeded = true;
            return false;
        }
        return true;
    }

    bool EndArray(rapidjson::SizeType) {
        size_t separator = static_cast<size_t>(m_prefix_len);
        for(int i = static_cast<int>(m_prefix_len) - 1; i >= 0; i--) {
            if (m_prefix[i] == '.') {
                separator = static_cast<size_t>(i);
                break;
            }
        }
        if (separator < m_prefix_len) {
            m_prefix[separator] = '\0';
            m_prefix_len = separator;
        }
        else {
            m_prefix[0] = '\0';
            m_prefix_len = 0;
        }
        m_current_depth--;
        return true;
    }

    bool Key(const char* k, size_t l, bool) {
        m_current_key.replace(0, l, std::string((const char*)k, l));
        m_current_key_len = l;
        m_current_key[m_current_key_len] = '\0';
        return true;
    }

    bool String(const char* v, size_t l, bool b) {
        std::string val = std::string((const char*)v, l);
        return addArgument(val);
    }

    bool Int(int64_t v) {
        std::string val = std::to_string(v);
        return addArgument(val);
    }

    bool Uint(uint64_t v) {
        std::string val = std::to_string(v);
        return addArgument(val);
    }

    bool Double(double v) {
        std::string val = std::to_string(v);
        return addArgument(val);
    }

    bool Bool(bool b) {
        if (b) {
            addArgument("true");
        } else {
            addArgument("false");
        }
        return true;
    }

    bool Null() {
        addArgument("");
        return true;
    }

    bool RawNumber(const char* str, rapidjson::SizeType length, bool) {
        std::string val = std::string((const char*)str, length);
        return addArgument(val);
    }

    bool Int64(int64_t i) {
        std::string val = std::string((const char*)i);
        return addArgument(val);
    }

    bool Uint64(uint64_t u) {
        std::string val = std::string((const char*)u);
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
    std::string    m_current_key;
    size_t         m_current_key_len;
    std::string    m_prefix;
    size_t         m_prefix_len;
    long int       m_arg_num_counter;
    long int       m_arg_num_limit;
    bool           m_arg_num_limit_exceeded;
    bool           m_silence;

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
    //CppSAXHandler handler(ctx);

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