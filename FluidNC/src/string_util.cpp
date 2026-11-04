#include "string_util.h"
#include <string>
#include <cstdlib>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>

const char* nullstr = "";

namespace string_util {
    struct EqIgnoreCase {
        bool operator()(char lhs, char rhs) const {
            return ::tolower(static_cast<unsigned char>(lhs)) == ::tolower(static_cast<unsigned char>(rhs));
        }
    };

    // cppcheck-suppress unusedFunction
    bool equal_ignore_case(std::string_view a, std::string_view b) {
        return std::equal(a.begin(), a.end(), b.begin(), b.end(), EqIgnoreCase());
    }

    // cppcheck-suppress unusedFunction
    bool starts_with_ignore_case(std::string_view a, std::string_view b) {
        if (a.length() < b.length()) {
            return false;
        }
        return std::equal(a.begin(), a.begin() + b.length(), b.begin(), EqIgnoreCase());
    }

    bool ends_with_ignore_case(const std::string_view a, const std::string_view b) {
        if (a.length() < b.length()) {
            return false;
        }
        return std::equal(b.begin(), b.end(), a.end() - b.length(), EqIgnoreCase());
    }

    // cppcheck-suppress unusedFunction
    const std::string_view trim(std::string_view s) {
        auto start = s.find_first_not_of(" \t\n\r\f\v");
        if (start == std::string_view::npos) {
            return s.substr(0, 0);
        }
        auto end = s.find_last_not_of(" \t\n\r\f\v");
        if (end == std::string_view::npos) {
            return s.substr(start);
        }
        return s.substr(start, end - start + 1);
    }

    bool from_xdigit(char c, uint8_t& value) {
        if (isdigit(c)) {
            value = c - '0';
            return true;
        }
        c = ::tolower(c);
        if (c >= 'a' && c <= 'f') {
            value = 10 + c - 'a';
            return true;
        }
        return false;
    }

    bool from_hex(std::string_view str, uint8_t& value) {
        value = 0;
        if (str.length() == 0 || str.length() > 2) {
            return false;
        }
        uint8_t x;
        while (str.length()) {
            value <<= 4;
            if (!from_xdigit(str[0], x)) {
                return false;
            }
            value += x;
            str = str.substr(1);
        }
        return true;
    }
    bool from_decimal(std::string_view str, uint32_t& value) {
        value = 0;
        if (str.empty()) {
            return false;
        }
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.length(), value);
        return ec == std::errc() && ptr == str.data() + str.length();
    }
    bool from_decimal(std::string_view sv, int32_t& value) {
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.length(), value);

        if (ec == std::errc::invalid_argument || ec == std::errc::result_out_of_range || ptr != sv.data() + sv.length()) {
            return false;
        }

        return true;
    }

    bool from_float(std::string_view sv, float& value) {
        // std::from_chars() does not work for float arguments on ESP32
        std::string s(sv);
        char*       floatEnd;
        const char* str = s.c_str();
        value           = strtof(str, &floatEnd);
        if (floatEnd != (str + sv.length())) {
            return false;
        }

        return true;
    }

    bool split(std::string_view& input, std::string_view& next, char delim) {
        auto pos = input.find_first_of(delim);
        if (pos != std::string_view::npos) {
            next  = input.substr(pos + 1);
            input = input.substr(0, pos);
            return true;
        }
        next = input.substr(0, 0);
        return false;
    }
    bool split_prefix(std::string_view& rest, std::string_view& prefix, char delim) {
        if (rest.empty()) {
            return false;
        }
        auto pos = rest.find_first_of(delim);
        if (pos != std::string_view::npos) {
            prefix = rest.substr(0, pos);
            rest   = rest.substr(pos + 1);
        } else {
            prefix = rest;
            rest   = rest.substr(0, 0);
        }
        return true;
    }

    size_t format_scaled(char* buf, size_t size, uint64_t value, int decimals, bool negative) {
        if (size == 0) {
            return 0;
        }
        // Digits are generated backwards into tmp: fraction, '.', integer part, sign.
        char  tmp[32];
        char* p = tmp + sizeof(tmp);
        for (int i = 0; i < decimals; ++i) {
            *--p = char('0' + value % 10);
            value /= 10;
        }
        if (decimals > 0) {
            *--p = '.';
        }
        do {
            *--p = char('0' + value % 10);
            value /= 10;
        } while (value);
        if (negative) {
            *--p = '-';
        }
        size_t len = tmp + sizeof(tmp) - p;
        if (len >= size) {
            len = size - 1;
        }
        memcpy(buf, p, len);
        buf[len] = '\0';
        return len;
    }

    // Exact rounding error of the product p = a * b (Dekker), so that
    // a * b == p + error exactly.  Avoids fma(), which newlib may not
    // implement exactly.
    static double product_error(double a, double b, double p) {
        const double split = 134217729.0;  // 2^27 + 1
        double       t     = split * a;
        double       ah    = t - (t - a);
        double       al    = a - ah;
        t                  = split * b;
        double bh          = t - (t - b);
        double bl          = b - bh;
        return ((ah * bh - p) + ah * bl + al * bh) + al * bl;
    }

    size_t format_fixed(char* buf, size_t size, double value, int decimals) {
        if (size == 0) {
            return 0;
        }
        if (decimals < 0) {
            decimals = 0;
        } else if (decimals > 9) {
            decimals = 9;
        }
        bool        negative = std::signbit(value);
        const char* special  = nullptr;
        if (std::isnan(value)) {
            special = negative ? "-nan" : "nan";
        } else if (std::isinf(value)) {
            special = negative ? "-inf" : "inf";
        }
        double scale = 1;
        for (int i = 0; i < decimals; ++i) {
            scale *= 10;
        }
        double scaled = std::fabs(value) * scale;
        if (!special && scaled >= 1e18) {
            // Too big for uint64_t; such values are nonsense in practice.
            int len = snprintf(buf, size, "%.*f", decimals, value);
            return len < 0 ? 0 : std::min(size_t(len), size - 1);
        }
        if (special) {
            size_t len = strlen(special);
            if (len >= size) {
                len = size - 1;
            }
            memcpy(buf, special, len);
            buf[len] = '\0';
            return len;
        }
        // Round half to even, as printf does, using the exact value of
        // |value| * scale = scaled + err.  For a value that is exactly a float
        // the product is exact (24 + 21 bits) and err is 0.
        double av  = std::fabs(value);
        double err = (double(float(av)) == av) ? 0 : product_error(av, scale, scaled);

        double   whole = std::floor(scaled);
        double   frac  = scaled - whole;
        uint64_t n     = uint64_t(whole);
        if (scaled >= 4503599627370496.0) {  // 2^52: scaled is an integer, err may exceed 1
            double e = std::floor(err);
            n += int64_t(e);
            frac = err - e;
            err  = 0;
        }
        // Below 2^52, frac and 0.5 are multiples of ulp(scaled) and |err| is at
        // most half of that, so err can only decide an exact tie.
        if (frac > 0.5 || (frac == 0.5 && (err > 0 || (err == 0 && (n & 1))))) {
            ++n;
        }
        return format_scaled(buf, size, n, decimals, negative);
    }
}
