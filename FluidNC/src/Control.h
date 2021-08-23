#pragma once

/*
  Control.h - collection of input pins
  Part of Grbl

  Copyright (c) 2012-2016 Sungeun K. Jeon for Gnea Research LLC

	2018 -	Bart Dring This file was modifed for use on the ESP32
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

#include "Configuration/Configurable.h"
#include "ControlPin.h"

class Control : public Configuration::Configurable {
private:
    // TODO: Should we not just put this in an array so we can enumerate it easily?

    ControlPin _safetyDoor;
    ControlPin _reset;
    ControlPin _feedHold;
    ControlPin _cycleStart;
    ControlPin _macro0;
    ControlPin _macro1;
    ControlPin _macro2;
    ControlPin _macro3;

public:
    Control();

    // Initializes control pins.
    void init();

    // Configuration handlers.
    void group(Configuration::HandlerBase& handler) override;

    bool system_check_safety_door_ajar();
    void report(char* status);

    ~Control() = default;
};
