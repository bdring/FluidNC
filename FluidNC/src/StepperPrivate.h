// Copyright (c) 2021 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

// Some useful constants.
const float DT_SEGMENT              = (1.0f / (float(ACCELERATION_TICKS_PER_SECOND) * 60.0f));  // min/segment
const float REQ_MM_INCREMENT_SCALAR = 1.25f;
const int   RAMP_ACCEL              = 0;
const int   RAMP_CRUISE             = 1;
const int   RAMP_DECEL              = 2;
const int   RAMP_DECEL_OVERRIDE     = 3;

struct PrepFlag {
    uint8_t recalculate : 1;
    uint8_t holdPartialBlock : 1;
    uint8_t parking : 1;
    uint8_t decelOverride : 1;
};

// Define Adaptive Multi-Axis Step-Smoothing(AMASS) levels and cutoff frequencies. The highest level
// frequency bin starts at 0Hz and ends at its cutoff frequency. The next lower level frequency bin
// starts at the next higher cutoff frequency, and so on. The cutoff frequencies for each level must
// be considered carefully against how much it over-drives the stepper ISR, the accuracy of the 16-bit
// timer, and the CPU overhead. Level 0 (no AMASS, normal operation) frequency bin starts at the
// Level 1 cutoff frequency and up to as fast as the CPU allows (over 30kHz in limited testing).
// For efficient computation, each cutoff frequency is twice the previous one.
// NOTE: AMASS cutoff frequency multiplied by ISR overdrive factor must not exceed maximum step frequency.
// NOTE: Current settings are set to overdrive the ISR to no more than 16kHz, balancing CPU overhead
// and timer accuracy.  Do not alter these settings unless you know what you are doing.

const uint32_t amassThreshold = Machine::Stepping::fStepperTimer / 8000;
const int      maxAmassLevel  = 3;  // Each level increase doubles the threshold
