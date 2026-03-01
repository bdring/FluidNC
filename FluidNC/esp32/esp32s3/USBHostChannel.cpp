// Copyright (c) 2026 - Algy Tynan
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#ifdef USB_HOST_ENABLED

#include "USBHostChannel.h"
#include "Serial.h"   // allChannels
#include "Report.h"   // log_info

// ---------------------------------------------------------------
// Constructor / Init
// ---------------------------------------------------------------

USBHostChannel::USBHostChannel() : Channel("usb_host", true) {
    _lineedit = new Lineedit(this, _line, Channel::maxLine - 1);
}

void USBHostChannel::init() {
    _driver = new USBHostDriver();
    _driver->init(_baud);

    setReportInterval(_report_interval_ms);
    allChannels.registration(this);

    if (_report_interval_ms) {
        log_info(name() << " created, baud " << _baud
                        << ", report interval " << _report_interval_ms << "ms");
    } else {
        log_info(name() << " created, baud " << _baud);
    }

    // Signal to any connected device that we're alive
    print("\n");
    out("RST", "MSG:");
}

// ---------------------------------------------------------------
// Configuration (YAML)
// ---------------------------------------------------------------

void USBHostChannel::group(Configuration::HandlerBase& handler) {
    handler.item("baud", _baud, 2400, 10000000);
    handler.item("report_interval_ms", _report_interval_ms);
    handler.item("message_level", _message_level, messageLevels2);
}

// ---------------------------------------------------------------
// Channel Read/Write -- delegate to USBHostDriver ring buffers
// ---------------------------------------------------------------

int USBHostChannel::read() {
    if (!_driver || !_driver->isConnected()) return -1;
    return _driver->read();
}

int USBHostChannel::peek() {
    if (!_driver || !_driver->isConnected()) return -1;
    return _driver->peek();
}

int USBHostChannel::available() {
    if (!_driver || !_driver->isConnected()) return 0;
    return _driver->available();
}

int USBHostChannel::rx_buffer_available() {
    if (!_driver || !_driver->isConnected()) return 0;
    return _driver->rxBufferAvailable();
}

void USBHostChannel::flushRx() {
    if (_driver) _driver->flushRx();
}

size_t USBHostChannel::write(uint8_t c) {
    if (!_driver || !_driver->isConnected()) return 0;
    return _driver->write(c);
}

size_t USBHostChannel::write(const uint8_t* buf, size_t len) {
    if (!_driver || !_driver->isConnected()) return 0;
    return _driver->write(buf, len);
}

// ---------------------------------------------------------------
// Lineedit delegation
// ---------------------------------------------------------------

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

#endif // USB_HOST_ENABLED
