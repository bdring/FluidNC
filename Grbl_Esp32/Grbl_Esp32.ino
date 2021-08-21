/*
  Grbl_ESP32.ino - Header for system level commands and real-time processes
  Part of Grbl
  Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC

	2018 -	Bart Dring This file was modified for use on the ESP32
					CPU. Do not use this with Grbl for atMega328P

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

#ifndef UNIT_TEST
#    include "src/Main.h"

void setup() {
#    ifndef GIT_REV
    static_assert(false,
                  "FluidNC is only maintained and supported on PlatformIO, because of its ease to handle libraries. Setting this up is a "
                  "matter of minutes, see instructions at https://github.com/bdring/Grbl_Esp32/wiki/Compiling-with-PlatformIO . You can "
                  "uncomment this message to proceed at your own risk.");
#        define GIT_REV "Unsupported build"
#    endif

    // #    ifdef DEBUG_PIN_DUMP
    //delay(2000);  // BJD removed mystery delay
    // #    endif

    main_init();
}

void loop() {
    run_once();
}

#endif
