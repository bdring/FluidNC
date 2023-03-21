// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

// #define false 0
// #define true 1

#include <WString.h>
#include <cstdint>
#include <esp_attr.h>
#include <xtensa/core-macros.h>
#include "Logging.h"

enum class DwellMode : uint8_t {
    Dwell      = 0,  // (Default: Must be zero)
    SysSuspend = 1,  //G92.1 (Do not alter value)
};

const float SOME_LARGE_VALUE = 1.0E+38f;

static inline int toMotor2(int axis) {
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

// Read a floating point value from a string. Line points to the input buffer, char_counter
// is the indexer pointing to the current character of the line, while float_ptr is
// a pointer to the result variable. Returns true when it succeeds
bool read_float(const char* line, size_t* char_counter, float* float_ptr);

// Blocking delay for very short time intervals
void delay_us(int32_t microseconds);

// Delay while checking for realtime characters and other events
bool delay_msec(uint32_t milliseconds, DwellMode mode);

// Delay without checking for realtime events.  Use only for short delays
void delay_ms(uint16_t ms);

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

bool  char_is_numeric(char value);
char* trim(char* value);

template <class T>
void swap(T& a, T& b) {
    T c(a);
    a = b;
    b = c;
}

// Short delays measured using the CPU cycle counter.  There is a ROM
// routine "esp_delay_us(us)" that almost does what what we need,
// except that it is in ROM and thus dodgy for use from ISRs.  We
// duplicate the esp_delay_us() here, but placed in IRAM, inlined,
// and factored so it can be used in different ways.

inline int32_t IRAM_ATTR getCpuTicks() {
    return XTHAL_GET_CCOUNT();
}

extern uint32_t g_ticks_per_us_pro;  // For CPU 0 - typically 240 MHz
extern uint32_t g_ticks_per_us_app;  // For CPU 1 - typically 240 MHz

inline int32_t IRAM_ATTR usToCpuTicks(int32_t us) {
    return us * g_ticks_per_us_pro;
}

inline int32_t IRAM_ATTR usToEndTicks(int32_t us) {
    return getCpuTicks() + usToCpuTicks(us);
}

// At the usual ESP32 clock rate of 240MHz, the range of this is
// just under 18 seconds, but it really should be used only for
// short delays up to a few tens of microseconds.

inline void IRAM_ATTR spinUntil(int32_t endTicks) {
    while ((getCpuTicks() - endTicks) < 0) {
#ifdef ESP32
        asm volatile("nop");
#endif
    }
}

void delay_us(int32_t us);

template <typename T>
T myMap(T x, T in_min, T in_max, T out_min, T out_max) {  // DrawBot_Badge
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

template <typename T>
T myConstrain(T in, T min, T max) {
    if (in < min) {
        return min;
    }
    if (in > max) {
        return max;
    }
    return in;
}

template <typename T>
T mapConstrain(T x, T in_min, T in_max, T out_min, T out_max) {
    x = myConstrain(x, in_min, in_max);
    return myMap(x, in_min, in_max, out_min, out_max);
}

// constrain a value and issue a message. Returns true is the value was OK
template <typename T>
bool constrain_with_message(T& value, T min, T max, const char* name = "") {
    if (value < min || value > max) {
        log_warn(name << " value " << value << " constrained to range (" << min << "," << max << ")");
        value = myConstrain(value, min, max);
        return false;
    }
    return true;
}

bool multiple_bits_set(uint32_t val);

String formatBytes(uint64_t bytes);
