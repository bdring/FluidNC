/*
    Part of Grbl_ESP32
    2021 -  Mitch Bradley

    Grbl_ESP32 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Grbl_ESP32 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Grbl_ESP32.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "Config.h"

#include "Configuration/Configurable.h"
#include "UartTypes.h"

#include <Stream.h>
#include <freertos/FreeRTOS.h>  // TickType_T

class Uart : public Stream, public Configuration::Configurable {
private:
    uart_port_t _uart_num;
    int         _pushback;

public:
    // These are public so that validators from classes
    // that use Uart can check that the setup is suitable.
    // E.g. some uses require an RTS pin.

    // Configurable.  Uart0 uses a fixed configuration
    int        baud     = 115200;
    UartData   dataBits = UartData::Bits8;
    UartParity parity   = UartParity::None;
    UartStop   stopBits = UartStop::Bits1;

    Pin _txd_pin;
    Pin _rxd_pin;
    Pin _rts_pin;
    Pin _cts_pin;

    Uart();
    Uart(int uart_num);

    bool   setHalfDuplex();
    bool   setPins(int tx_pin, int rx_pin, int rts_pin = -1, int cts_pin = -1);
    void   begin();
    void   begin(unsigned long baud, UartData dataBits, UartStop stopBits, UartParity parity);
    void   begin(unsigned long baud);
    int    available(void) override;
    int    read(void) override;
    int    read(TickType_t timeout);
    size_t readBytes(char* buffer, size_t length, TickType_t timeout);
    size_t readBytes(uint8_t* buffer, size_t length, TickType_t timeout) {
        return readBytes(reinterpret_cast<char*>(buffer), length, timeout);
    }
    size_t        readBytes(char* buffer, size_t length) override;
    int           peek(void) override;
    size_t        write(uint8_t data);
    size_t        write(const uint8_t* buffer, size_t length);
    inline size_t write(const char* buffer, size_t size) { return write(reinterpret_cast<const uint8_t*>(buffer), size); }
    size_t        write(const char* text);
    void          flush() { uart_flush(_uart_num); }
    bool          flushTxTimed(TickType_t ticks);

    // Configuration handlers:
    void validate() const override {
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

        handler.item("baud", baud);
        handler.item("mode", dataBits, parity, stopBits);
    }

    void config_message(const char* prefix, const char* usage);
};

extern Uart Uart0;

extern void uartInit();
