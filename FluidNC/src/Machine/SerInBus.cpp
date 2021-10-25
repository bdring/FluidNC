// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Copyright (c) 2021 -  Patrick Horton
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.


#include "SerInBus.h"
#include "Logging.h"
#include <Arduino.H>        // MSBFIRST for ahiftIn()
#include "Machine/MachineConfig.h"


// It's probably possible to implement I2SI pins and an I2SInBus as well
// (should be "I2SO" pins) that round-robin's using DMA.  The main difference
// would be that it would quicker and not use as much Core CPU time. But
// it is non-trivial and not clearly worth the effort.  This works.
//
// Polling in a task loop is not good for probes, not so bad for limit switches.
// I don't really want this thing to "know" about the probing state and try to
// make it a tiny bit more reponsive by allowing Probe to somehow call a
// synchronousRead() method ... which would also have to have to MUX on a read()
// in progress anyways.
//
// Don't like the hardwired number of pins.  It should allow for upto 32,
// yet somehow be efficient and only poll chunks of 8 as needed.
//
// LOL, wonder if you could implement this in terms of I2SO pins.
// For now I am (trying) to limit it to native GPIO pins.


namespace Machine
{
    uint32_t SerInBus::s_pins_used = 0;

    void SerInBus::validate() const
    {
        if (_clk.defined() || _latch.defined() || _data.defined())
        {
            Assert(_clk.defined(), "SERI CLK pin must be configured");
            Assert(_latch.defined(),"SERI Latch pin must be configured");
            Assert(_data.defined(), "SERI Data pin must be configured");
        }
    }


    void SerInBus::group(Configuration::HandlerBase& handler)
    {
        handler.item("clk_pin", _clk);
        handler.item("latch_pin", _latch);
        handler.item("data_pin", _data);
    }


    void SerInBus::init()
    {
        if (_clk.defined() &&
            _latch.defined() &&
            _data.defined())
        {

            m_clk_pin = _clk.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
            m_latch_pin  = _latch.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
            m_data_pin = _data.getNative(Pin::Capabilities::Input | Pin::Capabilities::Native);

            Assert(m_clk_pin, "could not get Native SERI CLK_pin");
            Assert(m_latch_pin,"could not get Native SERI Latch_pin");
            Assert(m_data_pin, "could not get Native SERI Data_pin");

            // set the number of byte to poll based on the
            // highest SERI pin number used ..

            for (int i=s_max_pins-1; i>=0; i--)
            {
                if (s_pins_used & (1 << i))
                {
                    m_num_poll_bytes = (i + 1) / 8;
                    break;
                }
            }

            log_info("SERI CLK:" << _clk.name() << " LATCH:" << _latch.name() << " DATA:" << _data.name() << " bytes:" << m_num_poll_bytes);

            if (!m_num_poll_bytes)
            {
                log_info("NOTE: SERI bus defined but no SERI pins defined");
            }

            _clk.setAttr(Pin::Attr::Output);
            _latch.setAttr(Pin::Attr::Output);
            _data.setAttr(Pin::Attr::Input);

            // if the SERI bus is created in the yaml, but
            // there are no pins, we go ahead and set the pin
            // attributes above, but do not start the task.

            if (m_num_poll_bytes)
            {
                xTaskCreatePinnedToCore(SerInBusTask,
                    "SerInBusTask",
                    4096,
                    NULL,
                    1,
                    nullptr,
                    CONFIG_ARDUINO_RUNNING_CORE);
            }
        }
        else
        {
            Assert(_clk.defined(), "SERI CLK_pin not configured");
            Assert(_latch.defined(),"SERI Latch_pin not configured");
            Assert(_data.defined(), "SERI Data_pin not configured");
        }
    }


    uint32_t SerInBus::read()
    {
        // note, I have not actually tested this yet with more than
        // one 74HC165, but it *should* work.

        _latch.write(1);    // digitalWrite(G_PIN_74HC165_LATCH, HIGH);

        uint32_t value = 0;
        for (int i=0; i<m_num_poll_bytes; i++)
        {
            uint32_t val = shiftIn(m_data_pin,m_clk_pin, MSBFIRST);
            value |= val << (8 * i);
        }
        m_value = value;

		_latch.write(0);    // digitalWrite(G_PIN_74HC165_LATCH, LOW);

        // dispatch fake interrupts
        // loop could be slightly optimized by keeping track
        // of the highest SERI pin with an interrupt, but for
        // now we check as many bytes as we poll.

        static uint32_t last_value = 0;
        if (last_value != m_value)
        {
            // log_debug("m_value_changed to " << String(m_value,HEX));
            if (m_fake_interrupt_mask)
            {
                for (int i=0; i<m_num_poll_bytes*8; i++)
                {
                    uint32_t mask = 1 << i;
                    if ((m_fake_interrupt_mask & mask) &&
                        (last_value & mask) != (m_value & mask))
                    {
                        // log_debug("issuing SERI fake Interrupt");
                        m_int_pins[i]->doFakeInterrupt();
                    }
                }
            }

            last_value = m_value;
        }

        return m_value;
    }


    // static
    void SerInBus::SerInBusTask(void *params)
    {
        SerInBus *self = config->_seri;
        Assert(self);

        while (1)
        {
            vTaskDelay(1);      // 100 times a second
            // if !probing
            self->read();
        }
    }

}   // namespace Machine
