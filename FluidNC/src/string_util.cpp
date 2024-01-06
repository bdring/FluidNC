#include "string_util.h"
#include <cstdlib>

namespace string_util {
    char tolower(char c) {
        if (c >= 'A' && c <= 'Z') {
            return c + ('a' - 'A');
        }
        return c;
    }

    bool equal_ignore_case(std::string_view a, std::string_view b) {
        return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](auto a, auto b) { return tolower(a) == tolower(b); });
    }

    bool starts_with_ignore_case(std::string_view a, std::string_view b) {
        return std::equal(a.begin(), a.begin() + b.size(), b.begin(), b.end(), [](auto a, auto b) { return tolower(a) == tolower(b); });
    }

    const std::string_view trim(std::string_view s) {
        auto start = s.find_first_not_of(" \t\n\r\f\v");
        if (start == std::string_view::npos) {
            return "";
        }
        auto end = s.find_last_not_of(" \t\n\r\f\v");
        if (end == std::string_view::npos) {
            return s.substr(start);
        }
        return s.substr(start, end - start + 1);
    }

    bool is_int(std::string_view s, int32_t& value) {
        char* end;
        value = std::strtol(s.cbegin(), &end, 10);
        return end == s.cend();
    }

    bool is_uint(std::string_view s, uint32_t& value) {
        char* end;
        value = std::strtoul(s.cbegin(), &end, 10);
        return end == s.cend();
    }

    bool is_float(std::string_view s, float& value) {
        char* end;
        value = std::strtof(s.cbegin(), &end);
        return end == s.cend();
    }
}
