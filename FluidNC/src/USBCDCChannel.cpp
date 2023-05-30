// Copyright (c) 2023 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "USBCDCChannel.h"
#if ARDUINO_USB_CDC_ON_BOOT

#include "Machine/MachineConfig.h"  // config
#include "Serial.h"                 // allChannels

USBCDCChannel::USBCDCChannel(bool addCR) : Channel("usbcdc", addCR) {
    _lineedit = new Lineedit(this, _line, Channel::maxLine - 1);
    _uart     = &Serial;
}

void USBCDCChannel::init() {
    allChannels.registration(this);
}

size_t USBCDCChannel::write(uint8_t c) {
    return _uart->write(c);
}

size_t USBCDCChannel::write(const uint8_t* buffer, size_t length) {
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

int USBCDCChannel::available() {
    return _uart->available();
}

int USBCDCChannel::peek() {
    return _uart->peek();
}

int USBCDCChannel::rx_buffer_available() {
    return 64-_uart->available();
}

bool USBCDCChannel::realtimeOkay(char c) {
    return _lineedit->realtime(c);
}

bool USBCDCChannel::lineComplete(char* line, char c) {
    if (_lineedit->step(c)) {
        _linelen        = _lineedit->finish();
        _line[_linelen] = '\0';
        strcpy(line, _line);
        _linelen = 0;
        return true;
    }
    return false;
}

Channel* USBCDCChannel::pollLine(char* line) {
    // UART0 is the only Uart instance that can be a channel input device
    // Other UART users like RS485 use it as a dumb character device
    if (_lineedit == nullptr) {
        return nullptr;
    }
    return Channel::pollLine(line);
}

int USBCDCChannel::read() {
    return _uart->read();
}

void USBCDCChannel::flushRx() {
    Channel::flushRx();
}

size_t USBCDCChannel::timedReadBytes(char* buffer, size_t length, TickType_t timeout) {
    // It is likely that _queue will be empty because timedReadBytes() is only
    // used in situations where the UART is not receiving GCode commands
    // and Grbl realtime characters.
    size_t remlen = length;
    while (remlen && _queue.size()) {
        *buffer++ = _queue.front();
        _queue.pop();
    }

    int avail = _uart->available();
    if (avail > remlen) {
        avail = remlen;
    }
    
    int res = int(_uart->read(buffer, remlen));
    // If res < 0, no bytes were read
    remlen -= (res < 0) ? 0 : res;
    return length - remlen;
}

#if ARDUINO_USB_CDC_ON_BOOT
USBCDCChannel Uart0(true);  // Primary serial channel with LF to CRLF conversion

void uartInit() {
    Serial.begin(115200);
    Uart0.init();
}
#endif
#endif
