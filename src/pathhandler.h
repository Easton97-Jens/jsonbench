#ifndef JSONBENCH_PATHHANDLER_H
#define JSONBENCH_PATHHANDLER_H

#include <cstddef>
#include <cstring>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "../config.h"

// Preserve structural boundaries: dots and NULs in keys are data, not delimiters
// to search when restoring a parent. The display convention remains historical
// dotted paths with repeated array keys and no numeric array indexes.
class PathHandler {
    struct Frame {
        std::size_t prefix_length;
        std::string key;
    };
    std::string prefix_;
    std::string key_;
    std::vector<Frame> frames_;

    void start(bool array) {
        if (m_error) return;
        const bool root_array = array && frames_.empty();
        const std::size_t length = root_array ? 5 :
            prefix_.size() + (prefix_.empty() ? 0 : 1) + key_.size();
        if (length >= JSON_STRING_SIZE) {
            m_error = true;
            return;
        }
        frames_.push_back({prefix_.size(), key_});
        if (root_array) {
            prefix_ = "array";
            key_ = "array";
        } else {
            if (!prefix_.empty()) prefix_ += '.';
            prefix_ += key_;
        }
    }

  public:
    bool m_error = false;

    void setKey(const char* key, std::size_t length) noexcept {
        if (m_error) return;
        if (length >= JSON_STRING_SIZE || (key == nullptr && length != 0)) {
            m_error = true;
            return;
        }
        try { key_.assign(key ? key : "", length); }
        catch (const std::exception&) { m_error = true; }
    }
    void push() noexcept {
        try { start(false); }
        catch (const std::exception&) { m_error = true; }
    }
    void startArraySpecial() noexcept {
        try { start(true); }
        catch (const std::exception&) { m_error = true; }
    }
    void pop() noexcept {
        if (m_error) return;
        if (frames_.empty()) {
            m_error = true;
            return;
        }
        prefix_.resize(frames_.back().prefix_length);
        key_ = std::move(frames_.back().key);
        frames_.pop_back();
    }
    std::string name() {
        const std::size_t length = prefix_.size() +
            (prefix_.empty() ? 0 : 1) + key_.size();
        if (m_error || length >= JSON_STRING_SIZE) {
            m_error = true;
            return {};
        }
        return prefix_.empty() ? key_ : prefix_ + '.' + key_;
    }
    // Compatibility for older callers; out must have JSON_STRING_SIZE bytes.
    void buildArgName(char* out) noexcept {
        try {
            const std::string value = name();
            if (m_error || out == nullptr) { m_error = true; return; }
            std::memcpy(out, value.data(), value.size());
            out[value.size()] = '\0';
        } catch (const std::exception&) { m_error = true; }
    }
};

#endif
