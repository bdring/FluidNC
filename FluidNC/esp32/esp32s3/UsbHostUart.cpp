// Copyright (c) 2026 - Algy Tynan
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include <soc/soc_caps.h>
#ifdef SOC_USB_OTG_SUPPORTED

#include "UsbHostUart.h"
#include "USBHostDriver.h"
#include "Report.h"

namespace {
    UartFactory::InstanceBuilder<UsbHostUart> registration("usb_host");
}

UsbHostUart::UsbHostUart(const char* name) : Uart(name) {}

UsbHostUart::~UsbHostUart() {
    if (_driver) {
        _driver->shutdown();
        delete _driver;
    }
}

void UsbHostUart::begin() {
    _driver = new USBHostDriver();
    _driver->init(_baud);

    if (!_driver->isInitialized()) {
        log_error("USB Host UART: driver init failed");
        return;
    }

    log_info("USB Host UART: baud " << _baud);
}

void UsbHostUart::begin(uint32_t baud, UartData dataBits, UartStop stopBits, UartParity parity) {
    _baud = baud;
    begin();
}

int UsbHostUart::read() {
    if (!_driver || !_driver->isConnected()) {
        return -1;
    }
    return _driver->read();
}

int UsbHostUart::peek() {
    if (!_driver || !_driver->isConnected()) {
        return -1;
    }
    return _driver->peek();
}

int UsbHostUart::available() {
    if (!_driver || !_driver->isConnected()) {
        return 0;
    }
    return _driver->available();
}

size_t UsbHostUart::write(uint8_t data) {
    if (!_driver || !_driver->isConnected()) {
        return 0;
    }
    return _driver->write(data);
}

size_t UsbHostUart::write(const uint8_t* buffer, size_t length) {
    if (!_driver || !_driver->isConnected()) {
        return 0;
    }
    return _driver->write(buffer, length);
}

void UsbHostUart::flushRx() {
    if (_driver) {
        _driver->flushRx();
    }
}

int UsbHostUart::rx_buffer_available() {
    if (!_driver || !_driver->isConnected()) {
        return 0;
    }
    return _driver->rx_buffer_available();
}

size_t UsbHostUart::timedReadBytes(char* buffer, size_t len, TickType_t timeout) {
    if (!_driver) {
        return 0;
    }

    size_t count = 0;
    TimeOut_t  xTimeOut;
    TickType_t xTicksRemaining = timeout;
    vTaskSetTimeOutState(&xTimeOut);

    while (count < len) {
        int c = _driver->read();
        if (c >= 0) {
            buffer[count++] = static_cast<char>(c);
        } else {
            if (xTaskCheckForTimeOut(&xTimeOut, &xTicksRemaining) != pdFALSE) {
                break;
            }
            vTaskDelay(1);
        }
    }

    return count;
}

void UsbHostUart::setSwFlowControl(bool on, uint32_t rx_threshold, uint32_t tx_threshold) {
    // Not applicable to USB VCP
}

void UsbHostUart::registerInputPin(pinnum_t pinnum, InputPin* pin) {
    // Not applicable to USB VCP
}

void UsbHostUart::group(Configuration::HandlerBase& handler) {
    handler.item("baud", _baud, 2400, 3000000);
}

#endif // SOC_USB_OTG_SUPPORTED
