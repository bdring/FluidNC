// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Copyright (c) 2021 -  Patrick Horton
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Configuration/Configurable.h"
#include "Pins/I2SIPinDetail.h"


namespace Machine {
    class I2SIBus : public Configuration::Configurable {
        // A Serial Input Bus for I2SI pins using 74HC165 or similar.
        // Supports upto 32 inputs using 3 ESP32 Native pins.
        // Uses I2SInput and real interrupts (2000 times per second)
        // or an optional task and shiftIn() to poll the 74HC165 100 times per second.
        // Clients call "value()" to get the most recent state.
        // In conjunction with I2SIPinDetail, can emulate/implement ISR's with
        // attachInterrupt() and detachInterrupt() calls,

    public:

        static const int s_max_pins = 32;
            // The maximum number of pins is limited by this object
            // storing them in a single uint32_t, so the maximum number
            // of 74HC165's that can be chained is four. The actual number
            // of pins used is determined by their declarations in
            // the yaml file. This object keeps track of the highest
            // pin number used and reads enough bytes to cover that
            // pin.  So if the yaml file only declares 0..5, only one
            // 74HC165 will be polled.

        I2SIBus() = default;

        void init();

        // there is currently no accessor for a synchronous read ...

        uint32_t IRAM_ATTR  value()
        {
            return s_value;
        }

        static void setPinUsed(int pin_num)
        {
            s_pins_used |= (1 << pin_num);
        }
        static uint32_t getPinsUsed()
        {
            return s_pins_used;
        }

        void attachInterrupt(int pin_num, Pins::I2SIPinDetail *pd)
        {
            if (pin_num + 1 > s_highest_interrupt)
                s_highest_interrupt = pin_num + 1;
            s_interrupt_mask |= (1 << pin_num);
            s_int_pins[pin_num] = pd;
        }
        void detachInterrupt(int pin_num)
        {
            s_interrupt_mask &= ~(1 << pin_num);
        }

        ~I2SIBus() = default;

        static void IRAM_ATTR handleValueChange(uint32_t value);
            // called directly from I2S interrupt handler and/or
            // from our shiftIn polling loop, sets the new value
            // and possibly dispatches interrupts.

    protected:

        void validate() const override;
        void group(Configuration::HandlerBase& handler) override;

        // config

        Pin _bck;
        Pin _ws;
        Pin _data;
        bool _use_shift_in = false;
        static int _s_num_chips;

        // native pins

        int m_bck_pin;          // CLK for shiftIn
        int m_ws_pin;           // LATCH for shiftIn
        int m_data_pin;         // DATA for shiftIn

        // implementation

        static uint32_t s_value;
        static uint32_t s_pins_used;
        static int s_highest_interrupt;    // pinnum+1
        static uint32_t s_interrupt_mask;
        static Pins::I2SIPinDetail *s_int_pins[s_max_pins];

        // methods

        uint32_t shiftInValue();
        static void shiftInTask(void *params);

    };
}
