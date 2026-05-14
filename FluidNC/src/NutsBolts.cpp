// Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Machine/MachineConfig.h"
#include "Protocol.h"  // protocol_exec_rt_system

#include <cstring>
#include <cstdint>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <string_view>

uint32_t get_ms() {
    return xTaskGetTickCount() * (1000 / configTICK_RATE_HZ);
}

void delay_ms(uint32_t ms) {
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

// Non-blocking delay function used for general operation and suspend features.
bool dwell_ms(uint32_t milliseconds, DwellMode mode) {
    while (milliseconds--) {
        if (mode == DwellMode::Dwell) {
            protocol_execute_realtime();
        } else {  // DwellMode::SysSuspend
            // Execute rt_system() only to avoid nesting suspend loops.
            protocol_exec_rt_system();
            if (sys.suspend().bit.restartRetract) {
                return false;  // Bail, if safety door reopens.
            }
        }
        if (sys.abort()) {
            return false;
        }
        delay_ms(1);
    }
    return true;
}

// Hypotenuse of a triangle
float hypot_f(float x, float y) {
    return sqrtf(x * x + y * y);
}

float vector_distance(float* v1, float* v2, size_t n) {
    float sum = 0.0;
    for (size_t i = 0; i < n; i++) {
        float d = v2[i] - v1[i];
        sum += d * d;
    }
    return sqrtf(sum);
}

float vector_length(float* v, size_t n) {
    float sum = 0.0;
    for (size_t i = 0; i < n; i++) {
        float d = v[i];
        sum += d * d;
    }
    return sqrtf(sum);
}

void scale_vector(float* v, float scale, size_t n) {
    for (size_t i = 0; i < n; i++) {
        v[i] *= scale;
    }
}

float convert_delta_vector_to_unit_vector(float* v) {
    auto  n_axis    = Axes::_numberAxis;
    float magnitude = vector_length(v, n_axis);
    scale_vector(v, 1.0f / magnitude, n_axis);
    return magnitude;
}

const float secPerMinSq = 60.0 * 60.0;  // Seconds Per Minute Squared, for acceleration conversion

float limit_acceleration_by_axis_maximum(float* unit_vec) {
    float limit_value = SOME_LARGE_VALUE;
    auto  n_axis      = Axes::_numberAxis;
    for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
        auto axisSetting = Axes::_axis[axis];
        if (unit_vec[axis] != 0) {  // Avoid divide by zero.
            limit_value = MIN(limit_value, fabsf(axisSetting->_acceleration / unit_vec[axis]));
        }
    }
    // The acceleration setting is stored and displayed in units of mm/sec^2,
    // but used in units of mm/min^2.  It suffices to perform the conversion once on
    // exit, since the limit computation above is independent of units - it simply
    // finds the smallest value.
    return limit_value * secPerMinSq;
}

float limit_rate_by_axis_maximum(float* unit_vec) {
    float limit_value = SOME_LARGE_VALUE;
    auto  n_axis      = Axes::_numberAxis;
    for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
        auto axisSetting = Axes::_axis[axis];
        if (unit_vec[axis] != 0) {  // Avoid divide by zero.
            limit_value = MIN(limit_value, fabsf(axisSetting->_maxRate / unit_vec[axis]));
        }
    }
    return limit_value;
}

bool char_is_numeric(char value) {
    return value >= '0' && value <= '9';
}

void trim(std::string_view& sv) {
    // Trim leading space
    while (sv.size() && ::isspace(sv.front())) {
        sv.remove_prefix(1);
    }
    while (sv.size() && ::isspace(sv.back())) {
        sv.remove_suffix(1);
    }
}

bool multiple_bits_set(uint32_t val) {
    // Takes advantage of a quirk of twos-complement math.  If a number has
    // only one bit set, for example 0b1000, then subtracting 1 will clear that
    // bit and set only other bits giving e.g. 0b0111, and anding the two gives 0.
    // If multiple bits are set, subtracting 1 will not clear the high bit.
    return val & (val - 1);
}

const char* to_hex(uint32_t n) {
    static char hexstr[12];
    snprintf(hexstr, 11, "0x%x", static_cast<unsigned int>(n));
    return hexstr;
}

std::string formatBytes(uint64_t bytes) {
    if (bytes < 1024) {
        return std::to_string((uint16_t)bytes) + " B";
    }
    float b = bytes;
    b /= 1024;
    if (b < 1024) {
        std::ostringstream msg;
        msg << std::fixed << std::setprecision(2) << b << " KB";
        return msg.str();
    }
    b /= 1024;
    if (b < 1024) {
        std::ostringstream msg;
        msg << std::fixed << std::setprecision(2) << b << " MB";
        return msg.str();
    }
    b /= 1024;
    if (b < 1024) {
        std::ostringstream msg;
        msg << std::fixed << std::setprecision(2) << b << " GB";
        return msg.str();
    }
    b /= 1024;
    if (b > 99999) {
        b = 99999;
    }
    std::ostringstream msg;
    msg << std::fixed << std::setprecision(2) << b << " TB";
    return msg.str();
}

std::string IP_string(uint32_t ipaddr) {
    std::string retval;
    retval += std::to_string(uint8_t((ipaddr >> 00) & 0xff)) + ".";
    retval += std::to_string(uint8_t((ipaddr >> 8) & 0xff)) + ".";
    retval += std::to_string(uint8_t((ipaddr >> 16) & 0xff)) + ".";
    retval += std::to_string(uint8_t((ipaddr >> 24) & 0xff));
    return retval;
}

void replace_string_in_place(std::string& subject, const std::string& search, const std::string& replace) {
    size_t pos = 0;
    while ((pos = subject.find(search, pos)) != std::string::npos) {
        subject.replace(pos, search.length(), replace);
        pos += replace.length();
    }
}
