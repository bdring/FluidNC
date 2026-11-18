#include "HWCDCChannel.h"

#include "NutsBolts.h"  // delay_ms
#include "Serial.h"     // allChannels

HWCDCChannel::HWCDCChannel(bool addCR) : Channel("hwcdc", addCR), _cdc(HWCDCSerial) {
    _lineedit = new Lineedit(this, _line, Channel::maxLine - 1);
}

void HWCDCChannel::init() {
    _cdc.setRxBufferSize(RX_BUFFER_SIZE);
    _cdc.begin(BAUD_RATE);
    allChannels.registration(this);
}

size_t HWCDCChannel::write(uint8_t c) {
    return _cdc.write(c);
}

size_t HWCDCChannel::write(const uint8_t* buffer, size_t length) {
    if (_addCR) {
        size_t rem      = length;
        char   lastchar = '\0';
        size_t j        = 0;
        while (rem) {
            const int bufsize = 80;
            uint8_t   modbuf[bufsize];
            size_t    k = 0;
            while (rem && k < (bufsize - 1)) {
                char c = buffer[j++];
                if (c == '\n' && lastchar != '\r') {
                    modbuf[k++] = '\r';
                }
                lastchar    = c;
                modbuf[k++] = c;
                --rem;
            }
            _cdc.write(modbuf, k);
        }
        return length;
    }
    return _cdc.write(buffer, length);
}

int HWCDCChannel::available() {
    return _cdc.available();
}

int HWCDCChannel::peek() {
    return _cdc.peek();
}

int HWCDCChannel::read() {
    return _cdc.read();
}

int HWCDCChannel::rx_buffer_available() {
    int used = _cdc.available();
    if (used < 0) {
        return 0;
    }
    return used < RX_BUFFER_SIZE ? RX_BUFFER_SIZE - used : 0;
}

void HWCDCChannel::flushRx() {
    while (_cdc.available()) {
        _cdc.read();
    }
    Channel::flushRx();
}

bool HWCDCChannel::realtimeOkay(char c) {
    return _lineedit->realtime(c);
}

bool HWCDCChannel::lineComplete(char* line, char c) {
    if (_lineedit->step(c)) {
        _linelen        = _lineedit->finish();
        _line[_linelen] = '\0';
        strcpy(line, _line);
        _linelen = 0;
        return true;
    }
    return false;
}

Error HWCDCChannel::pollLine(char* line) {
    if (_lineedit == nullptr) {
        return Error::NoData;
    }
    return Channel::pollLine(line);
}

size_t HWCDCChannel::timedReadBytes(char* buffer, size_t length, TickType_t timeout) {
    size_t remlen = length;

    while (remlen && _queue.size()) {
        *buffer++ = _queue.front();
        _queue.pop();
        --remlen;
    }

    while (remlen && timeout) {
        size_t n = _cdc.read((uint8_t*)buffer, remlen);
        buffer += n;
        remlen -= n;
        if (remlen) {
            delay_ms(1);
            --timeout;
        }
    }

    return length - remlen;
}
