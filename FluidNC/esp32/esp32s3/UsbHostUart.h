// Copyright (c) 2026 - Algy Tynan
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <soc/soc_caps.h>
#ifdef SOC_USB_OTG_SUPPORTED

#include "Uart.h"

class USBHostDriver;

class UsbHostUart : public Uart {
private:
    USBHostDriver* _driver = nullptr;

public:
    UsbHostUart(const char* name);
    ~UsbHostUart() override;

    void begin() override;
    void begin(uint32_t baud, UartData dataBits, UartStop stopBits, UartParity parity) override;

    int    peek() override;
    int    available() override;
    int    read() override;
    size_t write(uint8_t data) override;
    size_t write(const uint8_t* buffer, size_t length) override;

    void   flushRx() override;
    int    rx_buffer_available() override;
    size_t timedReadBytes(char* buffer, size_t len, TickType_t timeout) override;

    void setSwFlowControl(bool on, uint32_t rx_threshold, uint32_t tx_threshold) override;
    void registerInputPin(pinnum_t pinnum, InputPin* pin) override;

    void validate() override {}
    void group(Configuration::HandlerBase& handler) override;
};

#endif // SOC_USB_OTG_SUPPORTED
