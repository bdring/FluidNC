#pragma once

// this is the pulse range of a the servo. Typical servos are 0.001 to 0.002 seconds
// some servos have a wider range. You can adjust this here or in the calibration feature
const double SERVO_MIN_PULSE_SEC = 0.001;  // min pulse in seconds
const double SERVO_MAX_PULSE_SEC = 0.002;  // max pulse in seconds

const double SERVO_POSITION_MIN_DEFAULT = 0.0;   // mm
const double SERVO_POSITION_MAX_DEFAULT = 20.0;  // mm

const int SERVO_PULSE_FREQ = 50;  // 50Hz ...This is a standard analog servo value. Digital ones can repeat faster

const int SERVO_PULSE_RES_BITS  = 16;     // bits of resolution of PWM (16 is max)
const int SERVO_PULSE_RES_COUNT = 65535;  // see above  TODO...do the math here 2^SERVO_PULSE_RES_BITS

const double SERVO_TIME_PER_BIT = ((1.0 / (double)SERVO_PULSE_FREQ) / ((double)SERVO_PULSE_RES_COUNT));  // seconds
const uint32_t COUNTS_PER_SECOND = (uint32_t)(1.0 / SERVO_TIME_PER_BIT);  // seconds

const uint16_t SERVO_MIN_PULSE = (uint16_t)(SERVO_MIN_PULSE_SEC / SERVO_TIME_PER_BIT);  // in timer counts
const uint16_t SERVO_MAX_PULSE = (uint16_t)(SERVO_MAX_PULSE_SEC / SERVO_TIME_PER_BIT);  // in timer counts

const uint16_t SERVO_PULSE_RANGE = (SERVO_MAX_PULSE - SERVO_MIN_PULSE);

const double SERVO_TIMER_INT_FREQ = 50.0;  // Hz This is the task frequency
