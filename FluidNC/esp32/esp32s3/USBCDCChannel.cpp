// Copyright (c) 2023 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "USBCDCChannel.h"

#include "Machine/MachineConfig.h"  // config
#include "Serial.h"                 // allChannels
#include <HWCDC.h>

USBCDC       USBCDCSerial;
extern HWCDC USBSerial;

USBCDCChannel::USBCDCChannel(bool addCR) : Channel("usbcdc", addCR) {
    _lineedit = new Lineedit(this, _line, Channel::maxLine - 1);
    _cdc      = &USBCDCSerial;
}

#include "esp_event.h"
void cb(void* arg, esp_event_base_t base, int32_t id, void* data) {
    //   ::printf("Callback %p %d %d %p\n", arg, base, id, data);
}

void USBCDCChannel::init() {
    USB.begin();

    //    USBSerial.end();
    _cdc->begin(115200);
    _cdc->enableReboot(false);
    _cdc->onEvent(cb);
    delay_ms(300);  // Time for the host USB to reconnect
    allChannels.registration(this);
}

size_t USBCDCChannel::write(uint8_t c) {
    int actual = _cdc->write(c);
    if (actual != 1) {
        //        ::printf("dropped one\n");
    }

    return actual;
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
            size_t actual = _cdc->write(modbuf, k);
            if (actual != k) {
                //                ::printf("dropped %d\n", k - actual);
            }
        }
        return length;
    }
    size_t actual = _cdc->write(buffer, length);
    if (actual != length) {
        //        ::printf("dropped %d\n", length - actual);
    }
    return actual;
}

int USBCDCChannel::available() {
    return _cdc->available();
}

int USBCDCChannel::peek() {
    return _cdc->peek();
}

int USBCDCChannel::rx_buffer_available() {
    return 64 - _cdc->available();
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

Error USBCDCChannel::pollLine(char* line) {
    if (_lineedit == nullptr) {
        return Error::NoData;
    }
    return Channel::pollLine(line);
}

int USBCDCChannel::read() {
    return _cdc->read();
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

    int avail = _cdc->available();
    if (avail > remlen) {
        avail = remlen;
    }

    int res = int(_cdc->read(buffer, remlen));
    // If res < 0, no bytes were read
    remlen -= (res < 0) ? 0 : res;
    return length - remlen;
}
