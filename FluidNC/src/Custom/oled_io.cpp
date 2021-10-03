// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    OLED display code.

    It is designed to be used with a machine that has no easily accessible serial connection
    It shows basic status and connection information.

    When in alarm mode it will show the current Wifi/BT paramaters and status
    Most machines will start in alarm mode (needs homing)
    If the machine is running a job from SD it will show the progress
    In other modes it will show state and 3 axis DROs
    Thats All! 

    Library Info:
        https://github.com/ThingPulse/esp8266-oled-ssd1306


    Install to PlatformIO with this typed at the terminal
        platformio lib install 2978

*/

#include "../Config.h"

#ifdef INCLUDE_OLED_IO
#    include "oled_io.h"

#    include "../Pin.h"
#    include "../PinMapper.h"
#    include "../Uart.h"

SSD1306Wire* oled;

Pin       oled_sda_pin;
Pin       oled_scl_pin;
PinMapper oled_sda_pinmap;
PinMapper oled_scl_pinmap;
void      init_oled(uint8_t address, pinnum_t sda_gpio, pinnum_t scl_gpio, OLEDDISPLAY_GEOMETRY geometry) {
    Uart0 << "[MSG:INFO Init OLED SDA:gpio." << sda_gpio << " SCL:gpio." << scl_gpio << "]\n";
    oled = new SSD1306Wire(address, sda_gpio, scl_gpio, geometry, I2C_ONE, 400000);
    oled->init();
}

#endif
