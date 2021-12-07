// Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "NutsBolts.h"

#include "Machine/MachineConfig.h"
#include "Protocol.h"  // protocol_exec_rt_system

#include <cstring>
#include <cstdint>
#include <cmath>

const int MAX_INT_DIGITS = 8;  // Maximum number of digits in int32 (and float)

// Extracts a floating point value from a string. The following code is based loosely on
// the avr-libc strtod() function by Michael Stumpf and Dmitry Xmelkov and many freely
// available conversion method examples, but has been highly optimized for Grbl. For known
// CNC applications, the typical decimal value is expected to be in the range of E0 to E-4.
// Scientific notation is officially not supported by g-code, and the 'E' character may
// be a g-code word on some CNC systems. So, 'E' notation will not be recognized.
// NOTE: Thanks to Radu-Eosif Mihailescu for identifying the issues with using strtod().
bool read_float(const char* line, size_t* char_counter, float* float_ptr) {
    const char*   ptr = line + *char_counter;
    unsigned char c;
    // Grab first character and increment pointer. No spaces assumed in line.
    c = *ptr++;
    // Capture initial positive/minus character
    bool isnegative = false;
    if (c == '-') {
        isnegative = true;
        c          = *ptr++;
    } else if (c == '+') {
        c = *ptr++;
    }

    // Extract number into fast integer. Track decimal in terms of exponent value.
    uint32_t intval    = 0;
    int8_t   exp       = 0;
    size_t   ndigit    = 0;
    bool     isdecimal = false;
    while (1) {
        c -= '0';
        if (c <= 9) {
            ndigit++;
            if (ndigit <= MAX_INT_DIGITS) {
                if (isdecimal) {
                    exp--;
                }
                intval = intval * 10 + c;
            } else {
                if (!(isdecimal)) {
                    exp++;  // Drop overflow digits
                }
            }
        } else if (c == (('.' - '0') & 0xff) && !(isdecimal)) {
            isdecimal = true;
        } else {
            break;
        }
        c = *ptr++;
    }
    // Return if no digits have been read.
    if (!ndigit) {
        return false;
    }

    // Convert integer into floating point.
    float fval;
    fval = (float)intval;
    // Apply decimal. Should perform no more than two floating point multiplications for the
    // expected range of E0 to E-4.
    if (fval != 0) {
        while (exp <= -2) {
            fval *= 0.01f;
            exp += 2;
        }
        if (exp < 0) {
            fval *= 0.1f;
        } else if (exp > 0) {
            do {
                fval *= 10.0;
            } while (--exp > 0);
        }
    }
    // Assign floating point value with correct sign.
    if (isnegative) {
        *float_ptr = -fval;
    } else {
        *float_ptr = fval;
    }
    *char_counter = ptr - line - 1;  // Set char_counter to next statement
    return true;
}

void IRAM_ATTR delay_us(int32_t us) {
    spinUntil(usToEndTicks(us));
}

void delay_ms(uint16_t ms) {
    delay(ms);
}

// Non-blocking delay function used for general operation and suspend features.
bool delay_msec(uint32_t milliseconds, DwellMode mode) {
    while (milliseconds--) {
        pollChannels();
        if (mode == DwellMode::Dwell) {
            protocol_execute_realtime();
        } else {  // DwellMode::SysSuspend
            // Execute rt_system() only to avoid nesting suspend loops.
            protocol_exec_rt_system();
            if (sys.suspend.bit.restartRetract) {
                return false;  // Bail, if safety door reopens.
            }
        }
        if (sys.abort) {
            return false;
        }
        delay(1);
    }
    return true;
}

// Simple hypotenuse computation function.
float hypot_f(float x, float y) {
    return float(sqrt(x * x + y * y));
}

float convert_delta_vector_to_unit_vector(float* vector) {
    float magnitude = 0.0;
    auto  n_axis    = config->_axes->_numberAxis;
    for (size_t idx = 0; idx < n_axis; idx++) {
        if (vector[idx] != 0.0) {
            magnitude += vector[idx] * vector[idx];
        }
    }
    magnitude           = float(sqrt(magnitude));
    float inv_magnitude = 1.0f / magnitude;
    for (size_t idx = 0; idx < n_axis; idx++) {
        vector[idx] *= inv_magnitude;
    }
    return magnitude;
}

const float secPerMinSq = 60.0 * 60.0;  // Seconds Per Minute Squared, for acceleration conversion

float limit_acceleration_by_axis_maximum(float* unit_vec) {
    float limit_value = SOME_LARGE_VALUE;
    auto  n_axis      = config->_axes->_numberAxis;
    for (size_t idx = 0; idx < n_axis; idx++) {
        auto axisSetting = config->_axes->_axis[idx];
        if (unit_vec[idx] != 0) {  // Avoid divide by zero.
            limit_value = MIN(limit_value, float(fabs(axisSetting->_acceleration / unit_vec[idx])));
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
    auto  n_axis      = config->_axes->_numberAxis;
    for (size_t idx = 0; idx < n_axis; idx++) {
        auto axisSetting = config->_axes->_axis[idx];
        if (unit_vec[idx] != 0) {  // Avoid divide by zero.
            limit_value = MIN(limit_value, float(fabs(axisSetting->_maxRate / unit_vec[idx])));
        }
    }
    return limit_value;
}

bool char_is_numeric(char value) {
    return value >= '0' && value <= '9';
}

char* trim(char* str) {
    char* end;
    // Trim leading space
    while (::isspace((unsigned char)*str)) {
        str++;
    }
    if (*str == 0) {  // All spaces?
        return str;
    }
    // Trim trailing space
    end = str + ::strlen(str) - 1;
    while (end > str && ::isspace((unsigned char)*end)) {
        end--;
    }
    // Write new null terminator character
    end[1] = '\0';
    return str;
}

String formatBytes(uint64_t bytes) {
    if (bytes < 1024) {
        return String((uint16_t)bytes) + " B";
    }
    float b = bytes;
    b /= 1024;
    if (b < 1024) {
        return String(b, 2) + " KB";
    }
    b /= 1024;
    if (b < 1024) {
        return String(b, 2) + " MB";
    }
    b /= 1024;
    if (b < 1024) {
        return String(b, 2) + " GB";
    }
    b /= 1024;
    if (b > 99999) {
        b = 99999;
    }
    return String(b, 2) + " TB";
}
