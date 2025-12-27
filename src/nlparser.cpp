#include "nlparser.h"

#include <iostream>
#include <cstdio>
#include <vector>
#include <deque>
#include <string>

#ifdef HAVE_NLOHMANNJSON

#include "nlohmann/json.hpp"

using nljson = nlohmann::json;

class NLSAXHandler : public nlohmann::json_sax<nlohmann::json>{
  public:
    explicit NLSAXHandler():
    m_max_depth(0),
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
    ~NLSAXHandler() {};

    static bool init() {
        return true;
    }

    bool addArgument(const std::string& value) {
        if (m_silence) {
            return true;
        }

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
        std::cout << argname << ": " << argval << std::endl;
        m_arg_num_counter++;
        if (m_arg_num_limit > 0 &&
            m_arg_num_counter > m_arg_num_limit) {
            m_arg_num_limit_exceeded = true;
            std::cerr << "Argument number limit exceeded" << std::endl;
            return false;
        }
        return true;
    }

    /* NlohmannJSON mandatory methods */
    bool start_object(std::size_t elements) noexcept override {
        (void)elements;
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

    bool end_object() noexcept override {
        size_t separator = static_cast<size_t>(m_prefix_len);
        for(int i = static_cast<int>(m_prefix_len) - 1; i >= 0; i--) {
            if (m_prefix[i] == '.') {  // cppcheck-suppress useStlAlgorithm
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

    bool start_array(std::size_t elements) noexcept override {
        (void)elements;
        if (m_prefix_len == 0 && m_current_key_len == 0) {
            m_prefix = "array";
            m_prefix_len = m_prefix.size();
            m_current_key = "array";
            m_current_key_len = m_current_key.size();
        }
        else if (m_prefix_len > 0) {
            m_prefix.replace(m_prefix_len, 1, ".");
            m_prefix_len += 1;
            m_prefix.replace(m_prefix_len, m_current_key_len, m_current_key);
            m_prefix_len += m_current_key_len;
        }
        else {
            m_prefix.replace(0, m_current_key_len, m_current_key);
            m_prefix_len += m_current_key_len;
        }
        m_current_depth++;
        if (m_current_depth > m_max_depth) {
            m_depth_limit_exceeded = true;
            return false;
        }
        return true;
    }

    bool end_array() noexcept override {
        size_t separator = static_cast<size_t>(m_prefix_len);
        for(int i = static_cast<int>(m_prefix_len) - 1; i >= 0; i--) {
            if (m_prefix[i] == '.') {  // cppcheck-suppress useStlAlgorithm
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

    bool key(nljson::string_t& val) override {
        m_current_key.replace(0, val.size(), std::string(reinterpret_cast<const char*>(val.c_str()), val.size()));
        m_current_key_len = val.size();
        m_current_key[m_current_key_len] = '\0';
        return true;
    }

    bool string(nljson::string_t& v) override {
        std::string val = std::string(reinterpret_cast<const char*>(v.c_str()), v.size());
        return addArgument(val);
    }

    bool number_integer(nljson::number_integer_t v) override {
        std::string val = std::to_string(v);
        return addArgument(val);
    }

    bool number_unsigned(nljson::number_unsigned_t v) override {
        std::string val = std::to_string(v);
        return addArgument(val);
    }

    bool number_float(nljson::number_float_t v, const nljson::string_t& s) override {
        (void)s;
        std::string val = std::to_string(v);
        return addArgument(val);
    }

    bool null() override {
        addArgument("");
        return true;
    }

    bool boolean(bool b) override {
        if (b) {
            addArgument("true");
        } else {
            addArgument("false");
        }
        return true;
    }

    bool parse_error(std::size_t,
                     const std::string&,
                     const nlohmann::detail::exception&) override {
        return false;
    }

    bool binary(nlohmann::json::binary_t& val) override {
        (void)val;    // skip binary data
        return true;
    }
    /* NlohmannJSON mandatory methods END */

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

struct nl_parser {
    NLSAXHandler* impl;
};

extern "C" int nl_json_init(nl_parser **parser, char **error_msg) {
    assert(error_msg != NULL);
    assert(parser != NULL);

    try {
        *parser = new nl_parser{};
    }
    catch (const std::exception &e) {
        *parser = nullptr;
        *error_msg = strdup(e.what());
        return 1;
    }

    *error_msg = NULL;
    (*parser)->impl = new NLSAXHandler();
    (*parser)->impl->init();
    return 0;
}

/**
 * Frees the resources used for JSON parsing.
 */
extern "C" int nl_json_cleanup(nl_parser *parser) {
    delete(parser);
    return 0;
}

extern "C" int nl_parse_buffer(nl_parser *parser, const char *buf, unsigned int len,
                              char **error_msg) {
    if (!buf || !error_msg) return -1;


    bool ok = nlohmann::json::sax_parse(buf, buf + len, parser->impl);

    if (!ok) {
        fprintf(stderr, "NlohmannJSON error: parsing failed\n");
        return 2;
    }
    return 0;
}

extern "C" int nl_set_max_depth(nl_parser *parser, double max_depth) {
    parser->impl->setMaxDepth(max_depth);
    return 0;
}

extern "C" int nl_set_max_arg_num(nl_parser *parser, double max_arg_num) {
    parser->impl->setMaxArgNum(max_arg_num);
    return 0;
}

extern "C" int nl_set_silence(nl_parser *parser, int silence) {
    parser->impl->setSilence(silence);
    return 0;
}

extern "C" int nl_parser_cleanup(nl_parser *parser) {
    parser->impl->~NLSAXHandler();
    return 0;
}

#endif