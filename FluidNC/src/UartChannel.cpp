// Copyright (c) 2023 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "UartChannel.h"
#include "Machine/MachineConfig.h"  // config
#include "Serial.h"                 // allChannels

UartChannel::UartChannel(objnum_t num, bool addCR) : Channel("uart_channel", num, addCR) {
    _lineedit = new Lineedit(this, _line, Channel::maxLine - 1);
    _active   = false;
}

void UartChannel::init() {
    auto uart = config->_uarts[_uart_num];
    if (uart) {
        init(uart);
    } else {
        log_error(name() << ": missing uart" << _uart_num);
    }
    setReportInterval(_report_interval_ms);
}
void UartChannel::init(Uart* uart) {
    _uart = uart;
    allChannels.registration(this);
    if (_report_interval_ms) {
        log_info(name() << " created at report interval: " << _report_interval_ms);
    } else {
        log_info(name() << " created");
    }
    // Tell the channel listener that FluidNC has restarted.
    // The initial newline clears out any garbage characters that might have
    // resulted from the UART initialization and turn-on
    print("\n");
    out("RST", "MSG:");
    if (_uart_num) {
        getExpanderId();
    }
}

void UartChannel::getExpanderId() {
    out("ID", "EXP:");
    char   buf[128];
    size_t len;
    while ((len = _uart->timedReadBytes(buf, 128, 50)) != 0) {
        buf[len] = '\0';
        if (strncmp(buf, "(EXP,", 5) == 0) {
            auto pos = strrchr(buf, ')');
            if (pos) {
                *pos = '\0';
            }
            print("ok\n");
            log_info("IO Expander " << &buf[5]);
        }
    }
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
    auto c = _uart->read();
    if (c == 0x11) {
        // 0x11 is XON.  If we receive that, it is a request to use software flow control
        // 0 0 means use default values from uart.cpp
        _uart->setSwFlowControl(true, 0, 0);
        return -1;
    }
    return c;
}

void UartChannel::flushRx() {
    _uart->flushRx();
    Channel::flushRx();
}

size_t UartChannel::timedReadBytes(char* buffer, size_t length, TickType_t timeout) {
    size_t remlen = length;

    // It is likely that _queue will be empty because timedReadBytes() is only
    // used in situations where the UART is not receiving GCode commands
    // and Grbl realtime characters.
    while (_queue.size() && remlen) {
        *buffer++ = _queue.front();
        _queue.pop();
        --remlen;
    }

    auto thislen = _uart->timedReadBytes(buffer, remlen, timeout);
    remlen -= thislen;

    return length - remlen;
}

void UartChannel::out(const std::string& s, const char* tag) {
    log_stream(*this, "[" << tag << s);
}

void UartChannel::out_acked(const std::string& s, const char* tag) {
    log_stream(*this, "[" << tag << s);
}

void UartChannel::beginJSON(const char* json_tag) {
    //    out_acked(json_tag, "JSONBEGIN:");
}
void UartChannel::endJSON(const char* json_tag) {
    //    out_acked(json_tag, "JSONEND:");
}

void UartChannel::registerEvent(pinnum_t pinnum, InputPin* obj) {
    _uart->registerInputPin(pinnum, obj);
    Channel::registerEvent(pinnum, obj);
}

bool UartChannel::setAttr(pinnum_t index, bool* value, const std::string& attrString) {
    out(attrString, "EXP:");
    _ackwait = 1;
    for (size_t i = 0; i < 20; i++) {
        pollLine(nullptr);
        if (_ackwait < 1) {
            return _ackwait == 0;
        }
        delay_us(100);
    }
    _ackwait = 0;
    log_error("IO Expander is unresponsive");
    return false;
}
