// Copyright (c) 2026 - Algy Tynan
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once
#ifdef USB_HOST_ENABLED

#include "Channel.h"
#include "Configuration/Configurable.h"
#include "lineedit.h"
#include "USBHostDriver.h"

class USBHostChannel : public Channel, public Configuration::Configurable {
public:
    USBHostChannel();
    ~USBHostChannel() = default;

    // Channel interface
    void   init() override;
    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buf, size_t len) override;
    int    read() override;
    int    peek() override;
    int    available() override;
    int    rx_buffer_available() override;
    void   flushRx() override;
    bool   realtimeOkay(char c) override;
    bool   lineComplete(char* line, char c) override;

    // Configurable interface
    void group(Configuration::HandlerBase& handler) override;

private:
    Lineedit*      _lineedit = nullptr;
    USBHostDriver* _driver   = nullptr;

    // Config from YAML
    uint32_t _baud               = 1000000;
    int32_t  _report_interval_ms = 0;
};

#endif // USB_HOST_ENABLED
