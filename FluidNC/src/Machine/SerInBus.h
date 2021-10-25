// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Copyright (c) 2021 -  Patrick Horton
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Configuration/Configurable.h"
#include "Pins/SerInPinDetail.h"


namespace Machine {
    class SerInBus : public Configuration::Configurable {
        // A Serial Input Bus for SERI pins using 74HC165 or similar.
        // Supports upto 32 inputs using 3 ESP32 Native pins.
        // Sets up a task to poll the 74HC165 100 times per second.
        // Clients call "value()" to get the most recent state.
        // In conjunction with SerInPinDetail, can emulate ISR's with
        // attachFakeInterrupt() and detachFakeInterrupt() calls.

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

        SerInBus() = default;

        void init();

        // there is currently no accessor for a synchronous read ...

        uint32_t value()
        {
            return m_value;
        }

        static void setPinUsed(int pin_num)
        {
            s_pins_used |= (1 << pin_num);
        }
        static uint32_t getPinsUsed()
        {
            return s_pins_used;
        }

        void attachFakeInterrupt(int pin_num, Pins::SerInPinDetail *pd)
        {
            m_fake_interrupt_mask |= (1 << pin_num);
            m_int_pins[pin_num] = pd;
        }
        void detachFakeInterrupt(int pin_num)
        {
            m_fake_interrupt_mask &= ~(1 << pin_num);
        }

        ~SerInBus() = default;


    protected:

        void validate() const override;
        void group(Configuration::HandlerBase& handler) override;

        Pin _clk;
        Pin _latch;
        Pin _data;

        int m_clk_pin;      // native pins
        int m_latch_pin;
        int m_data_pin;

        uint32_t m_value = 0;
        int m_num_poll_bytes = 0;
        static uint32_t s_pins_used;
        uint32_t m_fake_interrupt_mask = 0;
        Pins::SerInPinDetail *m_int_pins[s_max_pins];

        uint32_t read();
        static void SerInBusTask(void *params);
            // needs better scheme for immediaate synchronous read during probing

    };
}
