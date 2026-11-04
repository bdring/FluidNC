#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace string_util {
    bool equal_ignore_case(std::string_view a, std::string_view b);
    bool starts_with_ignore_case(std::string_view a, std::string_view b);
    bool ends_with_ignore_case(std::string_view a, std::string_view b);

    const std::string_view trim(std::string_view s);

    bool from_xdigit(char c, uint8_t& value);
    bool from_hex(std::string_view str, uint8_t& value);
    bool from_decimal(std::string_view str, uint32_t& value);
    bool from_decimal(std::string_view str, int32_t& value);
    bool from_float(std::string_view str, float& value);
    bool split(std::string_view& input, std::string_view& next, char delim);
    bool split_prefix(std::string_view& rest, std::string_view& prefix, char delim);

    // Formats value with a fixed number of decimals (0-9), like
    // snprintf("%.*f"), using integer arithmetic.  newlib's floating-point
    // printf allocates per-task conversion state the first time a task uses
    // it and never frees it.  Values of 1e18 or more after scaling fall back
    // to snprintf.  Returns the length written, excluding the NUL.
    size_t format_fixed(char* buf, size_t size, double value, int decimals);

    // As above, formatting value / 10^decimals.
    size_t format_scaled(char* buf, size_t size, uint64_t value, int decimals, bool negative = false);
}
