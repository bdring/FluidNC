/*
    NullSpindle.cpp

    This is used when you don't want to use a spindle No I/O will be used
    and most methods don't do anything

    Part of Grbl_ESP32
    2020 -	Bart Dring

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
#include "NullSpindle.h"

#include "../System.h"  // sys.spindle_speed

#include <esp_attr.h>  // IRAM_ATTR

namespace Spindles {
    // ======================= Null ==============================
    // Null is just bunch of do nothing (ignore) methods to be used when you don't want a spindle

    void Null::init() {
        is_reversable = false;
        config_message();
        if (_speeds.size() == 0) {
            _speeds.push_back({ 0, 0 });
        }
    }
    void IRAM_ATTR Null::setSpeedfromISR(uint32_t dev_speed) {};
    void           Null::setState(SpindleState state, SpindleSpeed speed) {
        _current_state    = state;
        sys.spindle_speed = speed;
    }
    void Null::config_message() { log_info("No spindle"); }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<Null> registration("NoSpindle");
    }
}
