// Copyright (c) 2021 - Stefan de Bruijn
// Copyright (c) 2021 - Mitch Bradley
// Copyright (c) 2026 - FluidNC contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "ParlioBus.h"

namespace Machine {
    void ParlioBus::validate() {
        Assert(_clk.defined(), "PARLIO clk_pin must be configured");
        Assert(_clk.capabilities().has(Pin::Capabilities::Output | Pin::Capabilities::Native),
               "PARLIO clk_pin must be a GPIO output");

        bool anyData = false;
        for (int i = 0; i < NUM_DATA_PINS; ++i) {
            if (!_data[i].defined()) {
                continue;
            }
            anyData = true;
            Assert(_data[i].capabilities().has(Pin::Capabilities::Output | Pin::Capabilities::Native),
                   "PARLIO data%d_pin must be a GPIO output",
                   i);
            for (int j = 0; j < i; ++j) {
                if (_data[j].defined()) {
                    Assert(_data[j].index() != _data[i].index(), "PARLIO data pins must be unique");
                }
            }
        }
        Assert(anyData, "At least one PARLIO data pin must be configured");
    }

    void ParlioBus::group(Configuration::HandlerBase& handler) {
        // @config clk_pin
        // @default NO_PIN
        // @pin_attributes output
        // PARLIO output clock pin.
        handler.item("clk_pin", _clk);

        // @config drive_ma
        // @default 10
        // PARLIO pad drive strength in mA. Valid values are 5, 10, 20 and 40.
        handler.item("drive_ma", _drive_ma);
        if (_drive_ma != 5 && _drive_ma != 10 && _drive_ma != 20 && _drive_ma != 40) {
            _drive_ma = 10;
            throw AssertionFailed::create("PARLIO drive_ma must be 5, 10, 20, or 40");
        }

        // @config data0_pin
        // @default NO_PIN
        // @pin_attributes output
        // PARLIO data bit 0 output. Use only for stepper motor step and direction signals.
        handler.item("data0_pin", _data[0]);

        // @config data1_pin
        // @default NO_PIN
        // @pin_attributes output
        // PARLIO data bit 1 output. Use only for stepper motor step and direction signals.
        handler.item("data1_pin", _data[1]);

        // @config data2_pin
        // @default NO_PIN
        // @pin_attributes output
        // PARLIO data bit 2 output. Use only for stepper motor step and direction signals.
        handler.item("data2_pin", _data[2]);

        // @config data3_pin
        // @default NO_PIN
        // @pin_attributes output
        // PARLIO data bit 3 output. Use only for stepper motor step and direction signals.
        handler.item("data3_pin", _data[3]);

        // @config data4_pin
        // @default NO_PIN
        // @pin_attributes output
        // PARLIO data bit 4 output. Use only for stepper motor step and direction signals.
        handler.item("data4_pin", _data[4]);

        // @config data5_pin
        // @default NO_PIN
        // @pin_attributes output
        // PARLIO data bit 5 output. Use only for stepper motor step and direction signals.
        handler.item("data5_pin", _data[5]);

        // @config data6_pin
        // @default NO_PIN
        // @pin_attributes output
        // PARLIO data bit 6 output. Use only for stepper motor step and direction signals.
        handler.item("data6_pin", _data[6]);

        // @config data7_pin
        // @default NO_PIN
        // @pin_attributes output
        // PARLIO data bit 7 output. Use only for stepper motor step and direction signals.
        handler.item("data7_pin", _data[7]);

        // @config data8_pin
        // @default NO_PIN
        // @pin_attributes output
        // PARLIO data bit 8 output. Use only for stepper motor step and direction signals.
        handler.item("data8_pin", _data[8]);

        // @config data9_pin
        // @default NO_PIN
        // @pin_attributes output
        // PARLIO data bit 9 output. Use only for stepper motor step and direction signals.
        handler.item("data9_pin", _data[9]);

        // @config data10_pin
        // @default NO_PIN
        // @pin_attributes output
        // PARLIO data bit 10 output. Use only for stepper motor step and direction signals.
        handler.item("data10_pin", _data[10]);

        // @config data11_pin
        // @default NO_PIN
        // @pin_attributes output
        // PARLIO data bit 11 output. Use only for stepper motor step and direction signals.
        handler.item("data11_pin", _data[11]);

        // @config data12_pin
        // @default NO_PIN
        // @pin_attributes output
        // PARLIO data bit 12 output. Use only for stepper motor step and direction signals.
        handler.item("data12_pin", _data[12]);

        // @config data13_pin
        // @default NO_PIN
        // @pin_attributes output
        // PARLIO data bit 13 output. Use only for stepper motor step and direction signals.
        handler.item("data13_pin", _data[13]);

        // @config data14_pin
        // @default NO_PIN
        // @pin_attributes output
        // PARLIO data bit 14 output. Use only for stepper motor step and direction signals.
        handler.item("data14_pin", _data[14]);

        // @config data15_pin
        // @default NO_PIN
        // @pin_attributes output
        // PARLIO data bit 15 output. Use only for stepper motor step and direction signals.
        handler.item("data15_pin", _data[15]);
    }

    void ParlioBus::init() {
        _clk_native = _clk.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);

        for (int i = 0; i < NUM_DATA_PINS; ++i) {
            _data_defined[i] = _data[i].defined();
            if (_data_defined[i]) {
                _data_native[i] = _data[i].getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
            }
        }
    }
}
