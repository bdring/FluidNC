// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Logging.h"
#include "NativeSerial.h"

#include <HardwareSerial.h>

NativeSerial::NativeSerial() : Channel("native", true) {
    _lineedit = new Lineedit(this, _line, Channel::maxLine - 1);
}

// Use the specified baud rate
void NativeSerial::begin(unsigned long baudrate) {
    Serial.begin(baudrate);
}

// Use the configured baud rate
void NativeSerial::begin() {
    begin(static_cast<unsigned long>(baud));
}

int NativeSerial::available() {
    return Serial.available();
}

int NativeSerial::peek() {
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

int NativeSerial::read(TickType_t timeout) {
    if (_pushback != -1) {
        int ret   = _pushback;
        _pushback = -1;
        return ret;
    }
    return Serial.read();
}

int NativeSerial::read() {
    return read(0);
}

int NativeSerial::rx_buffer_available() {
    // 64?? TODO FIXME!
    return 64 - available();
}

bool NativeSerial::realtimeOkay(char c) {
    return _lineedit->realtime(c);
}

bool NativeSerial::lineComplete(char* line, char c) {
    if (_lineedit->step(c)) {
        _linelen        = _lineedit->finish();
        _line[_linelen] = '\0';
        strcpy(line, _line);
        _linelen = 0;
        return true;
    }
    return false;
}

Channel* NativeSerial::pollLine(char* line) {
    // UART0 is the only Uart instance that can be a channel input device
    // Other UART users like RS485 use it as a dumb character device
    if (_lineedit == nullptr) {
        return nullptr;
    }
    return Channel::pollLine(line);
}

size_t NativeSerial::timedReadBytes(char* buffer, size_t length, TickType_t timeout) {
    // It is likely that _queue will be empty because timedReadBytes() is only
    // used in situations where the UART is not receiving GCode commands
    // and Grbl realtime characters.
    size_t remlen = length;
    while (remlen && _queue.size()) {
        *buffer++ = _queue.front();
        _queue.pop();
    }

    int avail = Serial.available();
    if (avail < length) {
        avail = length;
    }
    int res = Serial.read(buffer, avail);

    // If res < 0, no bytes were read
    remlen -= (res < 0) ? 0 : res;
    return length - remlen;
}
size_t NativeSerial::write(uint8_t c) {
    // Use NativeSerial::write(buf, len) instead of uart_write_bytes() for _addCR
    return write(&c, 1);
}

size_t NativeSerial::write(const uint8_t* buffer, size_t length) {
    // Replace \n with \r\n
    if (_addCR) {
        size_t rem      = length;
        char   lastchar = '\0';
        size_t j        = 0;
        while (rem) {
            const int bufsize = 80;
            char      modbuf[bufsize];
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

            Serial.write(modbuf, k);
        }
    } else {
        Serial.write(buffer, length);
    }
    return length;
}

// size_t NativeSerial::write(const char* text) {
//    return uart_write_bytes(_uart_num, text, strlen(text));
// }

bool NativeSerial::flushTxTimed(TickType_t ticks) {
    Serial.flush(true);
    return true;
    //return uart_wait_tx_done(_uart_num, ticks) != ESP_OK;
}

void nativeSerialInit() {
    Uart0.begin(BAUD_RATE);
}

void NativeSerial::config_message(const char* prefix, const char* usage) {
    log_info(prefix << usage << "Native Uart. Baud:" << baud);
}

void NativeSerial::flushRx() {
    _pushback = -1;
    Serial.flush();
    Channel::flushRx();
}

bool NativeSerial::isConnected() {
    return Serial.available() || Serial.availableForWrite();
}

NativeSerial Uart0;  // Primary serial channel with LF to CRLF conversion
