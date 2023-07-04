// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "I2SOBus.h"
#include "../I2SOut.h"

namespace Machine {
    void I2SOBus::validate() {
        if (_bck.defined() || _data.defined() || _ws.defined()) {
            Assert(_bck.defined(), "I2SO BCK pin should be configured once");
            Assert(_data.defined(), "I2SO Data pin should be configured once");
            Assert(_ws.defined(), "I2SO WS pin should be configured once");
        }
    }

    void I2SOBus::group(Configuration::HandlerBase& handler) {
        handler.item("bck_pin", _bck);
        handler.item("data_pin", _data);
        handler.item("ws_pin", _ws);
    }

    void I2SOBus::init() {
        log_info("I2SO BCK:" << _bck.name() << " WS:" << _ws.name() << " DATA:" << _data.name());
        i2s_out_init();
    }
}
