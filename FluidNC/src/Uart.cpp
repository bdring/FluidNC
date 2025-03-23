// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
 * UART driver that accesses the ESP32 hardware FIFOs directly.
 */

#include "Uart.h"
#include <Driver/fluidnc_uart.h>

std::string encodeUartMode(UartData wordLength, UartParity parity, UartStop stopBits) {
    std::string s;
    s += std::to_string(int(wordLength) - int(UartData::Bits5) + 5);
    switch (parity) {
        case UartParity::Even:
            s += 'E';
            break;
        case UartParity::Odd:
            s += 'O';
            break;
        case UartParity::None:
            s += 'N';
            break;
    }
    switch (stopBits) {
        case UartStop::Bits1:
            s += '1';
            break;
        case UartStop::Bits1_5:
            s += "1.5";
            break;
        case UartStop::Bits2:
            s += '2';
            break;
    }
    return s;
}

const char* decodeUartMode(std::string_view str, UartData& wordLength, UartParity& parity, UartStop& stopBits) {
    str = string_util::trim(str);
    if (str.length() == 5 || str.length() == 3) {
        int32_t wordLenInt;
        if (!string_util::is_int(str.substr(0, 1), wordLenInt)) {
            return "Uart mode should be specified as [Bits Parity Stopbits] like [8N1]";
        } else if (wordLenInt < 5 || wordLenInt > 8) {
            return "Number of data bits for uart is out of range. Expected format like [8N1].";
        }
        wordLength = UartData(int(UartData::Bits5) + (wordLenInt - 5));

        switch (str[1]) {
            case 'N':
            case 'n':
                parity = UartParity::None;
                break;
            case 'O':
            case 'o':
                parity = UartParity::Odd;
                break;
            case 'E':
            case 'e':
                parity = UartParity::Even;
                break;
            default:
                return "Uart mode should be specified as [Bits Parity Stopbits] like [8N1]";
                break;  // Omits compiler warning. Never hit.
        }

        auto stop = str.substr(2, str.length() - 2);
        if (stop == "1") {
            stopBits = UartStop::Bits1;
        } else if (stop == "1.5") {
            stopBits = UartStop::Bits1_5;
        } else if (stop == "2") {
            stopBits = UartStop::Bits2;
        } else {
            return "Uart stopbits can only be 1, 1.5 or 2. Syntax is [8N1]";
        }

    } else {
        return "Uart mode should be specified as [Bits Parity Stopbits] like [8N1]";
    }
    return "";
}

Uart::Uart(int uart_num) : _uart_num(uart_num), _name("uart") {
    _name += std::to_string(uart_num);
}

void Uart::changeMode(unsigned long baud, UartData dataBits, UartParity parity, UartStop stopBits) {
    uart_mode(_uart_num, baud, dataBits, parity, stopBits);
}
void Uart::restoreMode() {
    changeMode(_baud, _dataBits, _parity, _stopBits);
}

void Uart::enterPassthrough() {
    changeMode(_passthrough_baud, _passthrough_databits, _passthrough_parity, _passthrough_stopbits);
}

void Uart::exitPassthrough() {
    restoreMode();
    if (_sw_flowcontrol_enabled) {
        setSwFlowControl(_sw_flowcontrol_enabled, _xon_threshold, _xoff_threshold);
    }
}

// This version is used for the initial console UART where we do not want to change the pins
void Uart::begin(unsigned long baud, UartData dataBits, UartStop stopBits, UartParity parity) {
    //    uart_driver_delete(_uart_num);
    changeMode(baud, dataBits, parity, stopBits);

    uart_init(_uart_num);
}

// This version is used when we have a config section with all the parameters
void Uart::begin() {
    auto txd = _txd_pin.getNative(Pin::Capabilities::UART | Pin::Capabilities::Output);
    auto rxd = _rxd_pin.getNative(Pin::Capabilities::UART | Pin::Capabilities::Input);
    auto rts = _rts_pin.undefined() ? -1 : _rts_pin.getNative(Pin::Capabilities::UART | Pin::Capabilities::Output);
    auto cts = _cts_pin.undefined() ? -1 : _cts_pin.getNative(Pin::Capabilities::UART | Pin::Capabilities::Input);

    if (setPins(txd, rxd, rts, cts)) {
        Assert(false, "Uart pin config failed");
        return;
    }

    begin(_baud, _dataBits, _stopBits, _parity);
    config_message("UART", std::to_string(_uart_num).c_str());
}

int Uart::read() {
    if (_pushback != -1) {
        int ret   = _pushback;
        _pushback = -1;
        return ret;
    }
    uint8_t c;
    int     res = uart_read(_uart_num, &c, 1, 0);
    return res == 1 ? c : -1;
}

size_t Uart::write(uint8_t c) {
    // Use Uart::write(buf, len) instead of uart_write_bytes() for _addCR
    return write(&c, 1);
}

size_t Uart::write(const uint8_t* buffer, size_t length) {
    return uart_write(_uart_num, buffer, length);
}

// size_t Uart::write(const char* text) {
//    return uart_write_bytes(_uart_num, text, strlen(text));
// }

size_t Uart::timedReadBytes(char* buffer, size_t len, TickType_t timeout) {
    int res = uart_read(_uart_num, (uint8_t*)buffer, len, timeout);
    // If res < 0, no bytes were read

    return res < 0 ? 0 : res;
}

void Uart::forceXon() {
    uart_xon(_uart_num);
}

void Uart::forceXoff() {
    uart_xoff(_uart_num);
}

void Uart::setSwFlowControl(bool on, int xon_threshold, int xoff_threshold) {
    _sw_flowcontrol_enabled = on;
    _xon_threshold          = xon_threshold;
    _xoff_threshold         = xoff_threshold;
    uart_sw_flow_control(_uart_num, on, xon_threshold, xoff_threshold);
}
void Uart::getSwFlowControl(bool& enabled, int& xon_threshold, int& xoff_threshold) {
    enabled        = _sw_flowcontrol_enabled;
    xon_threshold  = _xon_threshold;
    xoff_threshold = _xoff_threshold;
}
bool Uart::setHalfDuplex() {
    return uart_half_duplex(_uart_num);
}
bool Uart::setPins(int tx_pin, int rx_pin, int rts_pin, int cts_pin) {
    return uart_pins(_uart_num, tx_pin, rx_pin, rts_pin, cts_pin);
}
bool Uart::flushTxTimed(TickType_t ticks) {
    return uart_wait_output(_uart_num, ticks) != ESP_OK;
}

void Uart::config_message(const char* prefix, const char* usage) {
    log_info(prefix << usage << " Tx:" << _txd_pin.name() << " Rx:" << _rxd_pin.name() << " RTS:" << _rts_pin.name() << " Baud:" << _baud);
}

int Uart::rx_buffer_available(void) {
    return UART_FIFO_LEN - available();
}

int Uart::peek() {
    if (_pushback != -1) {
        return _pushback;
    }
    int ch = read();
    if (ch == -1) {
        return -1;
    }
    _pushback = ch;
    return ch;
}

int Uart::available() {
    return uart_buflen(_uart_num) + (_pushback != -1);
}

void Uart::flushRx() {
    _pushback = -1;
    uart_discard_input(_uart_num);
}

void Uart::registerInputPin(uint8_t pinnum, InputPin* pin) {
    uart_register_input_pin(_uart_num, pinnum, pin);
}
