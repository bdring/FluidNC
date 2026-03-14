#pragma once

#include <string>

namespace base64 {
inline std::string encode(const char* text) {
    return std::string("b64:") + (text ? text : "");
}
}
