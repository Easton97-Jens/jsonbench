#ifndef JSONBENCH_CPP_ARGUMENTS_H
#define JSONBENCH_CPP_ARGUMENTS_H

#include "pathhandler.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>

namespace jsonbench {

// Both native C++ adapters share this argument contract. Keep validation and
// counting active in silent mode, and preserve binary values with sized writes.
// PathHandler::name() records size errors, so the path is intentionally mutable.
template <typename OutputError>
void emit_argument(unsigned int& count, unsigned int maximum, bool silent,
        PathHandler& path, std::string_view value) {
    const std::string name = path.name();
    if (path.m_error) throw std::length_error("Argument name too long");
    if (value.size() >= JSON_STRING_SIZE)
        throw std::length_error("Argument value too long");
    if (count >= maximum)
        throw std::length_error("Argument number limit exceeded");
    ++count;
    if (silent) return;
    if (std::fwrite(name.data(), 1, name.size(), stdout) != name.size() ||
        std::fwrite(": ", 1, 2, stdout) != 2 ||
        std::fwrite(value.data(), 1, value.size(), stdout) != value.size() ||
        std::fputc('\n', stdout) == EOF)
        throw OutputError("Unable to write JSON argument output");
}

} // namespace jsonbench

#endif
