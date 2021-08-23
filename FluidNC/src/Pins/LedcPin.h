#pragma once

/*
    Ledc.h

    This is a driver for the ESP32 LEDC controller that is similar
    to the Arduino HAL driver for LEDC.  It differs from the Arduino
    driver by being able to handle output pin inversion in hardware,
    and by having the ledcSetDuty function in IRAM so it is safe
    to call it from ISRs.

    Part of Grbl_ESP32

    2021 -	Mitch Bradley

    Grbl is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    Grbl is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "../Pin.h"

extern int  ledcInit(Pin& pin, int chan, double freq, uint8_t bit_num);
extern void ledcSetDuty(uint8_t chan, uint32_t duty);
