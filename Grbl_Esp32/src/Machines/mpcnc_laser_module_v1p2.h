#pragma once
// clang-format off

/*
    mpcnc_v1p2.h
    Part of Grbl_ESP32

    Pin assignments for the Buildlog.net MPCNC controller
    https://github.com/bdring/Grbl_ESP32_MPCNC_Controller

    2019    - Bart Dring
    2020    - Mitch Bradley

    Grbl_ESP32 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Grbl is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Grbl_ESP32.  If not, see <http://www.gnu.org/licenses/>.
*/

// // Pin assignments for the Buildlog.net MPCNC controller

#define MACHINE_NAME "MPCNC_V1P2 with Laser Module"

// The laser module fires without a low signal. This keeps the enable on
#define LVL_SHIFT_ENABLE        "gpio.32"
#define CUSTOM_CODE_FILENAME    "Custom/mpcnc_laser_module.cpp"

#define DEFAULT_HOMING_SQUARED_AXES (bitnum_to_mask(X_AXIS) | bitnum_to_mask(Y_AXIS))

#define X_STEP_PIN      "gpio.12"
#define X2_STEP_PIN     "gpio.22"     // second motor

#define Y_STEP_PIN      "gpio.14"
#define Y2_STEP_PIN     "gpio.21"     // second motor

#define Z_STEP_PIN      "gpio.27"

#define X_DIRECTION_PIN "gpio.26"
#define X2_DIRECTION_PIN X_DIRECTION_PIN
#define Y_DIRECTION_PIN "gpio.25"
#define Y2_DIRECTION_PIN Y_DIRECTION_PIN
#define Z_DIRECTION_PIN "gpio.33"

// OK to comment out to use pin for other features
#define STEPPERS_DISABLE_PIN "gpio.13"

#define SPINDLE_TYPE            SpindleType::Laser
#define LASER_OUTPUT_PIN      "gpio.16"

#define COOLANT_MIST_PIN      "gpio.2"

#define X_LIMIT_PIN             "gpio.17"
#define Y_LIMIT_PIN             "gpio.4"
#define Z_LIMIT_PIN             "gpio.15"

#ifndef ENABLE_SOFTWARE_DEBOUNCE   // V1P2 does not have R/C filters
    #define ENABLE_SOFTWARE_DEBOUNCE
#endif

#define PROBE_PIN               "gpio.35"

// The default value in config.h is wrong for this controller
#ifdef INVERT_CONTROL_PIN_MASK
    #undef INVERT_CONTROL_PIN_MASK
#endif

#define INVERT_CONTROL_PIN_MASK B1110

#define CONTROL_RESET_PIN           "gpio.34"  // needs external pullup
#define CONTROL_FEED_HOLD_PIN       "gpio.36"  // needs external pullup
#define CONTROL_CYCLE_START_PIN     "gpio.39"  // needs external pullup

#define DEFAULT_STEP_PULSE_MICROSECONDS     3
#define DEFAULT_STEPPER_IDLE_LOCK_TIME      255 //  255 = Keep steppers on

#define DEFAULT_STEPPING_INVERT_MASK    0 // uint8_t
#define DEFAULT_DIRECTION_INVERT_MASK   0 // uint8_t
#define DEFAULT_INVERT_ST_ENABLE        0 // boolean
#define DEFAULT_INVERT_LIMIT_PINS       1 // boolean
#define DEFAULT_INVERT_PROBE_PIN        0 // boolean

#define DEFAULT_STATUS_REPORT_MASK 1

#define DEFAULT_SOFT_LIMIT_ENABLE 0 // false
#define DEFAULT_HARD_LIMIT_ENABLE 0  // false

#define DEFAULT_HOMING_ENABLE           1  // false
#define DEFAULT_HOMING_DIR_MASK         3 // move positive dir Z,negative X,Y
#define DEFAULT_HOMING_FEED_RATE        100.0 // mm/min
#define DEFAULT_HOMING_SEEK_RATE        200.0 // mm/min
#define DEFAULT_HOMING_DEBOUNCE_DELAY   250 // msec (0-65k)
#define DEFAULT_HOMING_PULLOFF          2.0 // mm

#ifdef USE_SPINDLE_RELAY
    #define DEFAULT_SPINDLE_RPM_MAX 1.0 // must be 1 so PWM duty is alway 100% to prevent relay damage
#else
    #define DEFAULT_SPINDLE_RPM_MAX 1000.0 // can be change to your spindle max
#endif

#define DEFAULT_SPINDLE_RPM_MIN 0.0 // rpm

#define DEFAULT_LASER_MODE 0 // false

#define DEFAULT_X_STEPS_PER_MM 200.0
#define DEFAULT_Y_STEPS_PER_MM 200.0
#define DEFAULT_Z_STEPS_PER_MM 800.0

#define DEFAULT_X_MAX_RATE 8000.0 // mm/min
#define DEFAULT_Y_MAX_RATE 8000.0 // mm/min
#define DEFAULT_Z_MAX_RATE 3000.0 // mm/min

#define DEFAULT_X_ACCELERATION 200.0 // mm/sec^2
#define DEFAULT_Y_ACCELERATION 200.0 // mm/sec^2
#define DEFAULT_Z_ACCELERATION 100.0 // mm/sec^2

#define DEFAULT_X_MAX_TRAVEL 500.0 // mm NOTE: Must be a positive value.
#define DEFAULT_Y_MAX_TRAVEL 500.0 // mm NOTE: Must be a positive value.
#define DEFAULT_Z_MAX_TRAVEL 80.0 // mm NOTE: Must be a positive value.
