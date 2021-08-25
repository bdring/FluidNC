// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
    Ledc.h

    This is a driver for the ESP32 LEDC controller that is similar
    to the Arduino HAL driver for LEDC.  It differs from the Arduino
    driver by being able to handle output pin inversion in hardware,
    and by having the ledcSetDuty function in IRAM so it is safe
    to call it from ISRs.
*/
#include "../Pin.h"

extern int  ledcInit(Pin& pin, int chan, double freq, uint8_t bit_num);
extern void ledcSetDuty(uint8_t chan, uint32_t duty);
