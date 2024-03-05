// Copyright (c) 2023 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "UartChannel.h"
#include "Machine/MachineConfig.h"  // config
#include "Serial.h"                 // allChannels

UartChannel::UartChannel(int num, bool addCR) : Channel("uart_channel", num, addCR) {
    _lineedit = new Lineedit(this, _line, Channel::maxLine - 1);
    _active   = false;
}

void UartChannel::init() {
    auto uart = config->_uarts[_uart_num];
    if (uart) {
        init(uart);
    } else {
        log_error("UartChannel: missing uart" << _uart_num);
    }
    setReportInterval(_report_interval_ms);
}
void UartChannel::init(Uart* uart) {
    _uart = uart;
    allChannels.registration(this);
    if (_report_interval_ms) {
        log_info("uart_channel" << _uart_num << " created at report interval: " << _report_interval_ms);
    } else {
        log_info("uart_channel" << _uart_num << " created");
    }
    log_msg_to(*this, "RST");
    // Give the extender a little time to process the command
    //    delay(100);
}

size_t UartChannel::write(uint8_t c) {
    return _uart->write(c);
}

size_t UartChannel::write(const uint8_t* buffer, size_t length) {
    // Replace \n with \r\n
    if (_addCR) {
        size_t rem      = length;
        char   lastchar = '\0';
        size_t j        = 0;
        while (rem) {
            const int bufsize = 80;
            uint8_t   modbuf[bufsize];
            // bufsize-1 in case the last character is \n
            size_t k = 0;
            while (rem && k < (bufsize - 1)) {
                char c = buffer[j++];
                if (c == '\n' && lastchar != '\r') {
                    modbuf[k++] = '\r';
                }
                lastchar    = c;
                modbuf[k++] = c;
                --rem;
            }
            _uart->write(modbuf, k);
        }
        return length;
    } else {
        return _uart->write(buffer, length);
    }
}

int UartChannel::available() {
    return _uart->available();
}

int UartChannel::peek() {
    return _uart->peek();
}

int UartChannel::rx_buffer_available() {
    return _uart->rx_buffer_available();
}

bool UartChannel::realtimeOkay(char c) {
    return _lineedit->realtime(c);
}

bool UartChannel::lineComplete(char* line, char c) {
    if (_lineedit->step(c)) {
        _linelen        = _lineedit->finish();
        _line[_linelen] = '\0';
        strcpy(line, _line);
        _linelen = 0;
        return true;
    }
    return false;
}

int UartChannel::read() {
    return _uart->read();
}

void UartChannel::flushRx() {
    _uart->flushRx();
    Channel::flushRx();
}

size_t UartChannel::timedReadBytes(char* buffer, size_t length, TickType_t timeout) {
    // It is likely that _queue will be empty because timedReadBytes() is only
    // used in situations where the UART is not receiving GCode commands
    // and Grbl realtime characters.
    size_t remlen = length;
    while (remlen && _queue.size()) {
        *buffer++ = _queue.front();
        _queue.pop();
    }

    int res = _uart->timedReadBytes(buffer, remlen, timeout);
    // If res < 0, no bytes were read
    remlen -= (res < 0) ? 0 : res;
    return length - remlen;
}

void UartChannel::out(const std::string& s, const char* tag) {
    std::string t(tag);
    log_msg_to(*this, t + s);
}

void UartChannel::out_acked(const std::string& s, const char* tag) {
    out(s, tag);
}

UartChannel Uart0(0, true);  // Primary serial channel with LF to CRLF conversion

void uartInit() {
    auto uart0 = new Uart(0);
    uart0->begin(BAUD_RATE, UartData::Bits8, UartStop::Bits1, UartParity::None);
    Uart0.init(uart0);
}
