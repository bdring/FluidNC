#pragma once

const int      SERVO_PWM_FREQ_DEFAULT = 50;  // 50Hz ...This is a standard analog servo value. Digital ones can repeat faster
const uint32_t SERVO_PWM_FREQ_MIN     = 50;
const uint32_t SERVO_PWM_FREQ_MAX     = 200;

const int      SERVO_PULSE_US_MIN_DEFAULT = 1000;
const int      SERVO_PULSE_US_MAX_DEFAULT = 2000;
const uint32_t SERVO_PULSE_US_MIN         = 500;
const uint32_t SERVO_PULSE_US_MAX         = 2500;

const int TIMER_MS_MIN = 20;
const int TIMER_MS_MAX = 250;
