/*
    Part of Grbl_ESP32
    2021 -  Stefan de Bruijn, Mitch Bradley

    Grbl_ESP32 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Grbl_ESP32 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Grbl_ESP32.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "I2SOBus.h"
#include "../I2SOut.h"

namespace Machine {
    void I2SOBus::validate() const {
        if (_bck.defined() || _data.defined() || _ws.defined()) {
            Assert(_bck.defined(), "I2SO BCK pin should be configured once");
            Assert(_data.defined(), "I2SO Data pin should be configured once");
            Assert(_ws.defined(), "I2SO WS pin should be configured once");
        }
    }

    void I2SOBus::group(Configuration::HandlerBase& handler) {
        handler.item("bck", _bck);
        handler.item("data", _data);
        handler.item("ws", _ws);
    }

    void I2SOBus::init() {
        log_info("I2SO BCK:" << _bck.name() << " WS:" << _ws.name() << " DATA:" << _data.name());
        i2s_out_init();
    }
}
