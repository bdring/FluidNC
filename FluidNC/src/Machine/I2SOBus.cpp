// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "I2SOBus.h"
#include "Driver/i2s_out.h"  // i2s_out_init()

namespace Machine {
    const EnumItem pulseUsValues[] = { { 1, "1" }, { 2, "2" }, { 4, "4" }, EnumItem(2) };

    void I2SOBus::validate() {
        Assert(_min_pulse_us == 1 || _min_pulse_us == 2 || _min_pulse_us == 4, "min_pulse_us must be 1, 2 or 4");
        if (_bck.defined() || _data.defined() || _ws.defined()) {
            Assert(_bck.defined(), "I2SO BCK pin should be configured once");
            Assert(_data.defined(), "I2SO Data pin should be configured once");
            Assert(_ws.defined(), "I2SO WS pin should be configured once");
        }
    }

    void I2SOBus::group(Configuration::HandlerBase& handler) {
        // @config bck_pin
        // @default NO_PIN
        // I2S bit-clock line, wired to the external I2S output shift-register chain.
        // Required (along with data_pin/ws_pin) if any i2so.N pin is used anywhere in the
        // config.
        handler.item("bck_pin", _bck);

        // @config data_pin
        // @default NO_PIN
        // I2S serial data line, wired to the external I2S output shift-register chain.
        handler.item("data_pin", _data);

        // @config ws_pin
        // @default NO_PIN
        // I2S word-select (latch) line, wired to the external I2S output shift-register
        // chain.
        handler.item("ws_pin", _ws);

        // @config min_pulse_us
        // @default 2
        // Minimum output pulse width, in microseconds, for an i2so.N pin -- one of 1, 2, or
        // 4 (see pulseUsValues above). Increase if downstream hardware needs a wider pulse
        // to register a state change reliably.
        handler.item("min_pulse_us", _min_pulse_us, pulseUsValues);

        // @config oe_pin
        // @default NO_PIN
        // Optional output-enable pin for the I2S shift-register chain.
        handler.item("oe_pin", _oe);
    }

    void I2SOBus::init() {
        log_info("I2SO BCK:" << _bck.name() << " WS:" << _ws.name() << " DATA:" << _data.name() << "Min Pulse:" << _min_pulse_us << "us");

        // Check capabilities:
        if (!_ws.capabilities().has(Pin::Capabilities::Output | Pin::Capabilities::Native)) {
            log_info("Not setting up I2SO: WS pin has incorrect capabilities");
            return;
        }
        if (!_bck.capabilities().has(Pin::Capabilities::Output | Pin::Capabilities::Native)) {
            log_info("Not setting up I2SO: BCK pin has incorrect capabilities");
            return;
        }
        if (!_data.capabilities().has(Pin::Capabilities::Output | Pin::Capabilities::Native)) {
            log_info("Not setting up I2SO: DATA pin has incorrect capabilities");
            return;
        }
        i2s_out_init_t params;
        params.ws_pin   = _ws.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
        params.bck_pin  = _bck.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
        params.data_pin = _data.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
        params.init_val = 0;

        params.min_pulse_us = _min_pulse_us;

        params.ws_drive_strength   = _ws.driveStrength();
        params.bck_drive_strength  = _bck.driveStrength();
        params.data_drive_strength = _data.driveStrength();

        i2s_out_init(&params);

        if (_oe.defined()) {
            log_info("I2SO OE is defined on " << _oe.name());
            _oe.setAttr(Pin::Attr::Output);
            _oe.off();
        }
    }
}
