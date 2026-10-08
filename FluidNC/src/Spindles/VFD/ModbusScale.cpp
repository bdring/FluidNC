// Copyright (c) 2024 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "ModbusScale.h"
#include "string_util.h"

namespace Spindles {
    namespace VFD {
        bool modbus_scale(uint32_t& n, std::string_view scale_str, uint32_t maxRPM) {
            if (scale_str.empty()) {
                return true;
            }
            uint64_t value   = n;
            uint64_t divider = 1;
            if (scale_str[0] == '%') {
                scale_str.remove_prefix(1);
                value *= 100;
                divider *= maxRPM;
            }
            if (!scale_str.empty() && scale_str[0] == '*') {
                std::string_view numerator_str;
                scale_str = scale_str.substr(1);
                string_util::split_prefix(scale_str, numerator_str, '/');
                uint32_t numerator;
                if (!string_util::from_decimal(numerator_str, numerator)) {
                    return false;
                }
                value *= numerator;
                if (!scale_str.empty()) {
                    uint32_t denominator;
                    if (!string_util::from_decimal(scale_str, denominator)) {
                        return false;
                    }
                    divider *= denominator;
                }
            } else if (!scale_str.empty() && scale_str[0] == '/') {
                uint32_t denominator;
                if (!string_util::from_decimal(scale_str.substr(1), denominator)) {
                    return false;
                }
                divider *= denominator;
            }
            if (divider == 0) {
                return false;
            }
            n = uint32_t(value / divider);
            return true;
        }
    }
}
