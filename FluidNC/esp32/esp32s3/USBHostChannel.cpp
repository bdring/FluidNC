// Copyright (c) 2026 - Algy Tynan
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include <soc/soc_caps.h>
#ifdef SOC_USB_OTG_SUPPORTED

#include "USBHostChannel.h"
#include "Serial.h"
#include "Report.h"
#include "Logging.h"

USBHostChannel::USBHostChannel() : Channel("usb_host", true) {
    _lineedit = new Lineedit(this, _line, Channel::maxLine - 1);
}

USBHostChannel::~USBHostChannel() {
    delete _lineedit;
    if (_driver) {
        _driver->shutdown();
        delete _driver;
    }
}

void USBHostChannel::init() {
    _driver = new USBHostDriver();
    _driver->init(_baud);

    if (!_driver->isInitialized()) {
        log_error(name() << " driver init failed -- channel not registered");
        return;
    }

    setReportInterval(_report_interval_ms);
    allChannels.registration(this);

    if (_report_interval_ms) {
        log_info(name() << " created, baud " << _baud
                        << ", report interval " << _report_interval_ms << "ms");
    } else {
        log_info(name() << " created, baud " << _baud);
    }

    print("\n");
    out("RST", "MSG:");
}

void USBHostChannel::group(Configuration::HandlerBase& handler) {
    handler.item("baud", _baud, 2400, 3000000);
    handler.item("report_interval_ms", _report_interval_ms);
    handler.item("message_level", _message_level, messageLevels2);
}

int USBHostChannel::read() {
    if (!_driver || !_driver->isConnected()) {
        return -1;
    }
    return _driver->read();
}

int USBHostChannel::peek() {
    if (!_driver || !_driver->isConnected()) {
        return -1;
    }
    return _driver->peek();
}

int USBHostChannel::available() {
    if (!_driver || !_driver->isConnected()) {
        return 0;
    }
    return _driver->available();
}

int USBHostChannel::rx_buffer_available() {
    if (!_driver || !_driver->isConnected()) {
        return 0;
    }
    return _driver->rx_buffer_available();
}

void USBHostChannel::flushRx() {
    if (_driver) {
        _driver->flushRx();
    }
    Channel::flushRx();
}

size_t USBHostChannel::write(uint8_t c) {
    if (!_driver || !_driver->isConnected()) {
        return 0;
    }
    return _driver->write(c);
}

size_t USBHostChannel::write(const uint8_t* buf, size_t len) {
    if (!_driver || !_driver->isConnected()) {
        return 0;
    }
    if (_addCR) {
        size_t rem      = len;
        char   lastchar = '\0';
        size_t j        = 0;
        while (rem) {
            const int bufsize = 80;
            uint8_t   modbuf[bufsize];
            size_t    k = 0;
            while (rem && k < (bufsize - 1)) {
                char c = buf[j++];
                if (c == '\n' && lastchar != '\r') {
                    modbuf[k++] = '\r';
                }
                lastchar    = c;
                modbuf[k++] = c;
                --rem;
            }
            _driver->write(modbuf, k);
        }
        return len;
    }
    return _driver->write(buf, len);
}

void USBHostChannel::out(const std::string& s, const char* tag) {
    log_stream(*this, "[" << tag << s);
}

void USBHostChannel::out_acked(const std::string& s, const char* tag) {
    log_stream(*this, "[" << tag << s);
}

size_t USBHostChannel::timedReadBytes(char* buffer, size_t length, TickType_t timeout) {
    size_t remlen = length;

    while (_queue.size() && remlen) {
        *buffer++ = _queue.front();
        _queue.pop();
        --remlen;
    }

    if (remlen && _driver) {
        TimeOut_t  xTimeOut;
        TickType_t xTicksRemaining = timeout;
        vTaskSetTimeOutState(&xTimeOut);

        while (remlen) {
            int c = _driver->read();
            if (c >= 0) {
                *buffer++ = static_cast<char>(c);
                --remlen;
            } else {
                if (xTaskCheckForTimeOut(&xTimeOut, &xTicksRemaining) != pdFALSE) {
                    break;
                }
                vTaskDelay(1);
            }
        }
    }

    return length - remlen;
}

bool USBHostChannel::realtimeOkay(char c) {
    return _lineedit->realtime(c);
}

bool USBHostChannel::lineComplete(char* line, char c) {
    if (_lineedit->step(c)) {
        _linelen        = _lineedit->finish();
        _line[_linelen] = '\0';
        strcpy(line, _line);
        _linelen = 0;
        return true;
    }
    return false;
}

#endif // SOC_USB_OTG_SUPPORTED
