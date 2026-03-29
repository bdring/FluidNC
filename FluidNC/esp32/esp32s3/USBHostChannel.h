// Copyright (c) 2026 - Algy Tynan
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <soc/soc_caps.h>
#ifdef SOC_USB_OTG_SUPPORTED

#include "Channel.h"
#include "Configuration/Configurable.h"
#include "lineedit.h"
#include "USBHostDriver.h"

class USBHostChannel : public Channel, public Configuration::Configurable {
private:
    Lineedit*      _lineedit = nullptr;
    USBHostDriver* _driver   = nullptr;

    uint32_t _baud               = 1000000;
    int32_t  _report_interval_ms = 0;

public:
    USBHostChannel();
    ~USBHostChannel();

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

    void   out(const std::string& s, const char* tag) override;
    void   out_acked(const std::string& s, const char* tag) override;

    size_t timedReadBytes(char* buffer, size_t length, TickType_t timeout) override;
    size_t timedReadBytes(uint8_t* buffer, size_t length, TickType_t timeout) {
        return timedReadBytes(reinterpret_cast<char*>(buffer), length, timeout);
    }

    void group(Configuration::HandlerBase& handler) override;
};

#endif // SOC_USB_OTG_SUPPORTED
