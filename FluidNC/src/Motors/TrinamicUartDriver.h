// Copyright (c) 2020 -	Bart Dring
// Copyright (c) 2020 -	The Ant Team
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TrinamicBase.h"
#include "../Pin.h"
#include "../Uart.h"

#include <cstdint>

namespace MotorDrivers {

    class TrinamicUartDriver : public TrinamicBase {
    public:
        TrinamicUartDriver() = default;

        void init() override;
        //void read_settings() override;
        //bool set_homing_mode(bool is_homing) override;
        void set_disable(bool disable) override;

        void debug_message();

        bool hw_serial_init();

        uint8_t _addr;

        // Configuration handlers:
        void validate() const override { StandardStepper::validate(); }

        void afterParse() override {
            StandardStepper::validate();
            Assert(_uart != nullptr, "TrinamicUartDriver must have a uart: subsection");
        }

        void group(Configuration::HandlerBase& handler) override {
            TrinamicBase::group(handler);
            // This is tricky.  The problem is that the UART is shared
            // between all TrinamicUartDriver instances (which is why
            // _uart is a static variable).  In the config file we
            // want exactly one uart: subsection underneath one of the
            // tmc220x: sections.  During the parsing phase of tree
            // traversal, _uart will start out as nullptr, so the
            // first time that a uart: section is seen, it will be
            // handled.  During other phases like Generate (called by
            // $CD), _uart will be non-null, so we use the instance
            // variable _addr to force the generation of only one
            // uart: section, beneath the tmc_220x: section for addr 0.
            handler.item("addr", _addr);
            if (_uart == nullptr || _addr == 0) {
                handler.section("uart", _uart);
            }            
        }

    protected:
        static Uart* _uart;

        static bool _uart_started;
        void        config_message() override;

        void finalInit();

        uint8_t toffValue();  // TO DO move to Base?

    private:
        bool test();
        void set_mode(bool isHoming);
        void trinamic_test_response();
        void trinamic_stepper_enable(bool enable);
    };

}