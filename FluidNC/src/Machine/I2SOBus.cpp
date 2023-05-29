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
        handler.item("oe_pin", _oe);
    }

    void I2SOBus::init() {
        log_info("I2SO BCK:" << _bck.name() << " WS:" << _ws.name() << " DATA:" << _data.name());

        portData_ = 0;

        _bck.setAttr(Pin::Attr::Output);
        _ws.setAttr(Pin::Attr::Output);
        _data.setAttr(Pin::Attr::Output);

        if (_oe.defined()) {
            log_info("I2SO OE is defined on " << _oe.name());

            // Output enable pin:
            push();

            _oe.setAttr(Pin::Attr::Output);
            _oe.off();
        }
    }

    void I2SOBus::write(int index, int high) {
        if (high) {
            portData_ |= bitnum_to_mask(index);
        } else {
            portData_ &= ~bitnum_to_mask(index);
        }
    }

    void I2SOBus::push() {
        _ws.off();
        for (int i = 0; i < NUMBER_PINS; i++) {
            _data.write(!!(portData_ & bitnum_to_mask(NUMBER_PINS - 1 - i)));
            _bck.on();
            _bck.off();
        }
        _ws.on();
    }
}
