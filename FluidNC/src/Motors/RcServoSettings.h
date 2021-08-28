#pragma once

const int SERVO_PWM_FREQ_DEFAULT = 50;  // 50Hz ...This is a standard analog servo value. Digital ones can repeat faster
const uint32_t SERVO_PWM_FREQ_MIN = 50;
const uint32_t SERVO_PWM_FREQ_MAX = 200;

const int SERVO_PULSE_US_MIN_DEFAULT = 1000;
const int SERVO_PULSE_US_MAX_DEFAULT = 2000;
const uint32_t SERVO_PULSE_US_MIN = 500;
const uint32_t SERVO_PULSE_US_MAX = 2500;

const int SERVO_PWM_RESOLUTION_BITS  = 16;     // bits of resolution of PWM (16 is max)
const int SERVO_PWM_MAX_DUTY = 65535;  // see above  TODO...do the math here 2^SERVO_PULSE_RES_BITS