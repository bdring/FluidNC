// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.
#pragma once

#include "Config.h"

#include "Configuration/Configurable.h"
#include "Configuration/GenericFactory.h"
#include "UartTypes.h"

#include <freertos/FreeRTOS.h>  // TickType_T

class Uart : public Stream, public Configuration::Configurable {
private:
    // One character of pushback for implementing peek().
    // We cannot use the queue for this because the queue
    // is after the check for realtime characters, whereas
    // peek() deals with characters before realtime ones
    // are handled.
    int _pushback = -1;

    bool setPins(pinnum_t tx_pin, pinnum_t rx_pin, pinnum_t rts_pin = -1, pinnum_t cts_pin = -1);

    uint32_t _uart_num = 0;  // Hardware UART engine number

    bool     _sw_flowcontrol_enabled = false;
    uint32_t _xon_threshold          = 0;
    uint32_t _xoff_threshold         = 0;

    std::string passthrough_mode = "";
    std::string _name;

public:
    // These are public so that validators from classes
    // that use Uart can check that the setup is suitable.
    // E.g. some uses require an RTS pin.

    // Configurable.  If the console is Uart0, it uses a fixed configuration
    uint32_t   _baud     = 115200;
    UartData   _dataBits = UartData::Bits8;
    UartParity _parity   = UartParity::None;
    UartStop   _stopBits = UartStop::Bits1;

    uint32_t   _passthrough_baud     = 0;
    UartData   _passthrough_databits = UartData::Bits8;
    UartParity _passthrough_parity   = UartParity::Even;
    UartStop   _passthrough_stopbits = UartStop::Bits1;

    Pin _txd_pin;
    Pin _rxd_pin;
    Pin _rts_pin;
    Pin _cts_pin;

    // Name is required for the configuration factory to work.
    const char* name() { return _name.c_str(); }

    Uart(uint32_t uart_num = -1);
    void begin();
    void begin(uint32_t baud, UartData dataBits, UartStop stopBits, UartParity parity);

    // Stream methods - Uart must inherit from Stream because the TMCStepper library
    // needs a Stream instance.
    int peek(void) override;
    int available(void) override;
    int read(void) override;

    // Print methods (Stream inherits from Print)
    size_t write(uint8_t data) override;
    size_t write(const uint8_t* buffer, size_t length) override;

    // Support methods for UartChannel
    void   flushRx();
    int    rx_buffer_available(void);
    size_t timedReadBytes(char* buffer, size_t len, TickType_t timeout);
    size_t timedReadBytes(uint8_t* buffer, size_t len, TickType_t timeout) { return timedReadBytes((char*)buffer, len, timeout); }

    // Used by VFDSpindle
    bool flushTxTimed(TickType_t ticks);

    // Used by VFDSpindle and Dynamixel2
    bool setHalfDuplex();

    void forceXon();
    void forceXoff();

    void setSwFlowControl(bool on, uint32_t rx_threshold, uint32_t tx_threshold);
    void getSwFlowControl(bool& enabled, uint32_t& rx_threshold, uint32_t& tx_threshold);
    void changeMode(uint32_t baud, UartData dataBits, UartParity parity, UartStop stopBits);
    void restoreMode();

    void enterPassthrough();
    void exitPassthrough();

    void registerInputPin(pinnum_t pinnum, InputPin* pin);

    // Configuration handlers:
    void validate() override {
        Assert(!_txd_pin.undefined(), "UART: TXD is undefined");
        Assert(!_rxd_pin.undefined(), "UART: RXD is undefined");
        // RTS and CTS are optional.
    }

    void afterParse() override {}

    void group(Configuration::HandlerBase& handler) override {
        handler.item("txd_pin", _txd_pin);
        handler.item("rxd_pin", _rxd_pin);
        handler.item("rts_pin", _rts_pin);
        handler.item("cts_pin", _cts_pin);

        handler.item("baud", _baud, 2400, 10000000);
        handler.item("mode", _dataBits, _parity, _stopBits);
        handler.item("passthrough_baud", _passthrough_baud, 0, 10000000);  // 0 means not configured
        handler.item("passthrough_mode", _passthrough_databits, _passthrough_parity, _passthrough_stopbits);
    }

    void config_message(const char* prefix, const char* usage);
};

using UartFactory = Configuration::GenericFactory<Uart>;
