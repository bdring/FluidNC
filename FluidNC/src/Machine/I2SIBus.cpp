// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Copyright (c) 2021 -  Patrick Horton
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.


#include "I2SIBus.h"
#include "Logging.h"
#include <Arduino.H>        // MSBFIRST for ahiftIn()
#include "Machine/MachineConfig.h"
#include "I2SIn.h"


// #define MONITOR_SHIFTIN


namespace Machine
{
    uint32_t I2SIBus::s_pins_used = 0;
    int I2SIBus::_s_num_chips= 1;
    uint32_t I2SIBus::s_value = 0;
    int I2SIBus::s_highest_interrupt = 0;    // pinnum+1
    uint32_t I2SIBus::s_interrupt_mask = 0;
    Pins::I2SIPinDetail *I2SIBus::s_int_pins[s_max_pins];


    void I2SIBus::validate() const
    {
        if (_bck.defined() || _ws.defined() || _data.defined())
        {
            Assert(_bck.defined(), "I2SI bck_pin must be configured");
            Assert(_ws.defined(),"I2SI ws_in must be configured");
            Assert(_data.defined(), "I2SI data_pin must be configured");
            Assert(_s_num_chips>0 && _s_num_chips<5,"I2SI num_chips must be 1..4");
        }
    }


    void I2SIBus::group(Configuration::HandlerBase& handler)
    {
        handler.item("bck_pin",      _bck);
        handler.item("ws_pin",       _ws);
        handler.item("data_pin",     _data);
        handler.item("use_shift_in", _use_shift_in);
        handler.item("num_chips",    _s_num_chips);
    }


    void I2SIBus::init()
    {
        if (_bck.defined() &&
            _ws.defined() &&
            _data.defined())
        {
            m_bck_pin = _bck.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
            m_ws_pin  = _ws.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
            m_data_pin = _data.getNative(Pin::Capabilities::Input | Pin::Capabilities::Native);

            Assert(m_bck_pin, "could not get Native I2SI bck_pin");
            Assert(m_ws_pin,"could not get Native I2SI ws_pin");
            Assert(m_data_pin, "could not get Native I2SI data_pin");
            Assert(_s_num_chips>0 && _s_num_chips<5,"num_chips must be 1..4");

            log_info(
                "I2SI BCK:" << _bck.name() <<
                " WS:" << _ws.name() <<
                " DATA:" << _data.name() <<
                (_use_shift_in?" SHIFT_IN":"") <<
                " num_chips:" << _s_num_chips);

            // log_debug(" pins_used=" << String(s_pins_used,HEX));

            if (!s_pins_used)
            {
                log_info("NOTE: I2SI bus defined but no I2SI pins defined");
                return;
            }

            _bck.setAttr(Pin::Attr::Output);
            _ws.setAttr(Pin::Attr::Output);
            _data.setAttr(Pin::Attr::Input);

            if (_use_shift_in)
            {
                xTaskCreatePinnedToCore(
                    shiftInTask,
                    "shiftInTask",
                    4096,
                    NULL,
                    1,
                    nullptr,
                    CONFIG_ARDUINO_RUNNING_CORE);
            }
            else
            {
                i2s_in_init(
                    m_ws_pin,
                    m_bck_pin,
                    m_data_pin,
                    _s_num_chips);
            }
        }
        else
        {
            Assert(_bck.defined(), "I2SI bck_pin not configured");
            Assert(_ws.defined(),"I2SI ws_pin not configured");
            Assert(_data.defined(),"I2SI data_pin not configured");
        }
    }


    uint32_t I2SIBus::shiftInValue()
        // Only called from the !i2s task, shift in
        // 8 bits for each 74HC165 chip, ending with
        // the first one in the chain in the LSB nibble
    {
        _ws.write(1);       // latch
        uint32_t value = 0;
        for (int i=0; i<_s_num_chips; i++)
        {
            uint32_t val = shiftIn(m_data_pin,m_bck_pin, MSBFIRST);
            value  = value << 8 | val;
        }
        _ws.write(0);       // unlatch
        // log_debug("SerinBus shiftInValue=" << String(value,HEX));
        return value;
    }



    // static
    void IRAM_ATTR I2SIBus::handleValueChange(uint32_t value)
        // This method is called as a real interrupt handler, directly from
        // the I2S isr if !_use_shift_in.  So serial debugging is generally not allowed.
        // The debugging can only be turned on manually when _use_shift_in ..
    {
        // log_debug("handleValueChange(" << String(value,HEX) << ") s_value=" << String(s_value,HEX));
        // log_debug("int_mask=" << String(s_interrupt_mask,HEX) << " highest=" << s_highest_interrupt);

        uint32_t prev = s_value;
        s_value = value;
        if (s_interrupt_mask)
        {
            for (int i=0; i<s_highest_interrupt; i++)
            {
                uint32_t mask = 1 << i;
                if ((s_interrupt_mask & mask) &&
                    (prev & mask) != (value & mask))
                {
                    // log_debug("issuing I2SI Interrupt " << i);
                    s_int_pins[i]->doInterrupt();
                }
            }
        }
    }


    // static
    void I2SIBus::shiftInTask(void *params)
    {
        I2SIBus *self = config->_i2si;
        Assert(self);
        while (1)
        {
            vTaskDelay(10);      // 100 times a second
            uint32_t value = self->shiftInValue();
            if (s_value != value)
                handleValueChange(value);

            #ifdef MONITOR_SHIFTIN
                static uint32_t last_out = 0;
                static uint32_t shift_counter = 0;

                shift_counter++;
                uint32_t now = millis();
                if (now > last_out + 2000)  // every 2 seconds
                {
                    last_out = now;
                    log_debug("shift counter=" << shift_counter << " value=" << String(s_value,HEX));
                }
            #endif
        }
    }

}   // namespace Machine
