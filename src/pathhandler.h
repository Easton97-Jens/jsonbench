#include <cstddef>
#include <cstring>

#include "config.h"  // needs for JSON_STRING_SIZE (default 8192)

class PathHandler {
  public:
    bool m_error;
    PathHandler() {
        m_prefix[0] = '\0';
        m_current_key[0] = '\0';
        m_prefix_len = 0;
        m_current_key_len = 0;
        m_error = false;
    }

    // set object key
    void setKey(const char* key, size_t len) {
        if (m_current_key_len + len >= JSON_STRING_SIZE) {
            m_error = true;
            return;
        }
        std::memcpy(m_current_key, key, len);
        m_current_key[len] = '\0';
        m_current_key_len = len;
    }

    // start_object '{' / start_array '['
    void push() {
        if (m_prefix_len == 0) {
            std::memcpy(m_prefix, m_current_key, m_current_key_len);
            m_prefix_len = m_current_key_len;
        } else {
            if (m_prefix_len + 1 + m_current_key_len >= JSON_STRING_SIZE) {
                // new key would be too long
                m_error = true;
                return;
            }
            m_prefix[m_prefix_len++] = '.';
            std::memcpy(m_prefix + m_prefix_len, m_current_key, m_current_key_len);
            m_prefix_len += m_current_key_len;
        }
        m_prefix[m_prefix_len] = '\0';
    }

    // end_object '}' / end_array ']'
    void pop() {
        if (m_prefix_len == 0) return;

        size_t separator = m_prefix_len;
        for (int i = static_cast<int>(m_prefix_len) - 1; i >= 0; --i) {
            if (m_prefix[i] == '.') {
                separator = static_cast<size_t>(i);
                break;
            }
        }

        if (separator < m_prefix_len) {
            size_t new_len = m_prefix_len - separator - 1;
            std::memcpy(m_current_key, m_prefix + separator + 1, new_len);
            m_current_key[new_len] = '\0';
            m_current_key_len = new_len;

            m_prefix[separator] = '\0';
            m_prefix_len = separator;
        } else {
            std::memcpy(m_current_key, m_prefix, m_prefix_len);
            m_current_key[m_prefix_len] = '\0';
            m_current_key_len = m_prefix_len;

            m_prefix[0] = '\0';
            m_prefix_len = 0;
        }
    }

    // array - special case because we use 'array' prefix in case of v2
    void startArraySpecial() {
        if (m_prefix_len == 0 && m_current_key_len == 0) {
            std::memcpy(m_prefix, "array", 5);
            m_prefix_len = 5;
            std::memcpy(m_current_key, "array", 5);
            m_current_key_len = 5;
            m_prefix[5] = '\0';
            m_current_key[5] = '\0';
        } else {
            push();  // normal push
        }
    }

    // build argument name from m_prefix
    void buildArgName(char* out) {
        if (m_prefix_len > 0) {
            if (m_prefix_len + m_current_key_len + 1 > JSON_STRING_SIZE) {
                m_error = true;
                return;
            }
            std::memcpy(out, m_prefix, m_prefix_len);
            std::memcpy(out + m_prefix_len, ".", 1);
            std::memcpy(out + m_prefix_len + 1, m_current_key, m_current_key_len);
            out[m_prefix_len + 1 + m_current_key_len] = '\0';
        } else {
            if (m_current_key_len > JSON_STRING_SIZE) {
                m_error = true;
                return;
            }
            std::memcpy(out, m_current_key, m_current_key_len);
            out[m_current_key_len] = '\0';
        }
    }

    // may be these will be needed later...
    // const char* getPrefix() const { return m_prefix; }
    // size_t getPrefixLen() const { return m_prefix_len; }

  private:
        char   m_prefix[JSON_STRING_SIZE];
        size_t m_prefix_len;
        char   m_current_key[JSON_STRING_SIZE];
        size_t m_current_key_len;
};
