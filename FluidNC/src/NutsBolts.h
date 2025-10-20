// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

// #define false 0
// #define true 1

#include <cstdint>
#include <string_view>
#include "Types.h"
#include "Logging.h"
#include "Driver/delay_usecs.h"

enum class DwellMode : uint8_t {
    Dwell      = 0,  // (Default: Must be zero)
    SysSuspend = 1,  //G92.1 (Do not alter value)
};

const float SOME_LARGE_VALUE = 1.0E+38f;

static inline motor_t toMotor2(axis_t axis) {
    return axis + MAX_N_AXIS;
}

// Conversions
const float MM_PER_INCH = (25.40f);
const float INCH_PER_MM = (0.0393701f);

// Useful macros
#define clear_vector(a) memset(a, 0, sizeof(a))
#ifndef MAX
#    define MAX(a, b) (((a) > (b)) ? (a) : (b))  // changed to upper case to remove conflicts with other libraries
#endif
#ifndef MIN
#    define MIN(a, b) (((a) < (b)) ? (a) : (b))  // changed to upper case to remove conflicts with other libraries
#endif
#define isequal_position_vector(a, b) !(memcmp(a, b, sizeof(float) * MAX_N_AXIS))

// Bit field and masking macros
// bitnum_to_mask(n) is similar to bit(n) as defined in Arduino.h.
// We define our own version so we can apply the static_cast, thus making it work with scoped enums,
// using a different name to avoid name conflicts and include ordering issues with Arduino.h
#define bitnum_to_mask(n) (1 << static_cast<unsigned int>(n))

#define set_bits(target, mask) (target) |= (mask)
#define clear_bits(target, mask) (target) &= ~(mask)
#define bits_are_true(target, mask) ((target & (mask)) != 0)
#define bits_are_false(target, mask) ((target & (mask)) == 0)
#define set_bitnum(target, num) (target) |= bitnum_to_mask(num)
#define clear_bitnum(target, num) (target) &= ~bitnum_to_mask(num)
#define bitnum_is_true(target, num) ((target & bitnum_to_mask(num)) != 0)
#define bitnum_is_false(target, num) ((target & bitnum_to_mask(num)) == 0)

// Read a floating point value from a string. Line points to the input buffer, pos
// is the indexer pointing to the current character of the line, while float_ptr is
// a pointer to the result variable. Returns true when it succeeds
bool read_float(const char* line, size_t& pos, float& result);

// Get the current "time" in millisecond ticks
uint32_t get_ms();

// Delay while checking for realtime characters and other events
bool dwell_ms(uint32_t milliseconds, DwellMode mode = DwellMode::Dwell);

// Delay without checking for realtime events.  Use only for short delays
void delay_ms(uint32_t ms);

// Computes hypotenuse, avoiding avr-gcc's bloated version and the extra error checking.
float hypot_f(float x, float y);

// Distance between endpoints of n-vectors
float vector_distance(float* v1, float* v2, size_t n);

// Length of an n-vector
float vector_length(float* v, size_t n);

// Multiply a vector by a scale factor
void scale_vector(float* v, float scale, size_t n);

float convert_delta_vector_to_unit_vector(float* vector);
float limit_acceleration_by_axis_maximum(float* unit_vec);
float limit_rate_by_axis_maximum(float* unit_vec);

const char* to_hex(uint32_t n);

bool char_is_numeric(char value);
void trim(std::string_view& sv);

template <typename I, typename O>
O myMap(I x, const I in_min, const I in_max, O out_min, O out_max) {
    return static_cast<O>((x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min);
}

template <typename T>
T myConstrain(T in, const T min, const T max) {
    if (in < min) {
        return min;
    }
    if (in > max) {
        return max;
    }
    return in;
}

template <typename I, typename O>
O mapConstrain(I x, const I in_min, const I in_max, O out_min, O out_max) {
    x = myConstrain(x, in_min, in_max);
    return myMap(x, in_min, in_max, out_min, out_max);
}

// constrain a value and issue a message. Returns true is the value was OK
template <typename T>
bool constrain_with_message(T& value, const T min, const T max, const char* name = "") {
    if (value < min || value > max) {
        log_warn(name << " value " << value << " constrained to range (" << min << "," << max << ")");
        value = myConstrain(value, min, max);
        return false;
    }
    return true;
}

bool multiple_bits_set(uint32_t val);

std::string formatBytes(uint64_t bytes);

std::string IP_string(uint32_t ipaddr);

void replace_string_in_place(std::string& subject, const std::string& search, const std::string& replace);

template <typename D, typename S>
void copyArray(D* dest, S src, size_t n) {
    for (size_t i = 0; i < n; i++) {
        dest[i] = src[i];
    }
}

template <typename D, typename S>
void addArray(D* dest, S* src, size_t n) {
    for (size_t i = 0; i < n; i++) {
        dest[i] += src[i];
    }
}

template <typename D, typename S>
void addArray(D* dest, S addend, size_t n) {
    for (size_t i = 0; i < n; i++) {
        dest[i] += addend;
    }
}

template <typename D, typename S>
void subtractArray(D* dest, S* src, size_t n) {
    for (size_t i = 0; i < n; i++) {
        dest[i] -= src[i];
    }
}

template <typename D, typename S>
void subtractArray(D* dest, S subtrahend, size_t n) {
    for (size_t i = 0; i < n; i++) {
        dest[i] -= subtrahend;
    }
}

template <typename D, typename S>
void multiplyArray(D* dest, S* src, size_t n) {
    for (size_t i = 0; i < n; i++) {
        dest[i] *= src[i];
    }
}

template <typename D, typename S>
void multiplyArray(D* dest, S factor, size_t n) {
    for (size_t i = 0; i < n; i++) {
        dest[i] *= factor;
    }
}

template <typename D, typename S>
void setArray(D* dest, S value, size_t n) {
    for (size_t i = 0; i < n; i++) {
        dest[i] = value;
    }
}

template <typename S>
void logArray(const char* legend, S* src, size_t n) {
    std::string s(legend);
    for (size_t i = 0; i < n; i++) {
        s += " ";
        s += std::to_string(src[i]);
    }
    log_debug(s);
}
