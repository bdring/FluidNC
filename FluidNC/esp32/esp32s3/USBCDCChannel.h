#pragma once

#include <sdkconfig.h>
#include <esp_idf_version.h>

#if defined(CONFIG_TINYUSB_CDC_ENABLED) && ESP_IDF_VERSION_MAJOR < 5
#    include <USB.h>
// We need this even when using TinyUSB in order to stop the HWCDC interface
#    include <HWCDC.h>
#endif

#include "Channel.h"
#include "Configuration/Configurable.h"
#include "lineedit.h"

class USBCDCChannel : public Channel, public Configuration::Configurable {
private:
    Lineedit* _lineedit;
    USBCDC&   _cdc;

    // Config from YAML
    int32_t _report_interval_ms = 0;

public:
    USBCDCChannel(bool addCR = false);

    void init() override;

    // Print methods (Stream inherits from Print)
    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buf, size_t len) override;

    // Stream methods (Channel inherits from Stream)
    int peek(void) override;
    int available(void) override;
    int read() override;

    // Channel methods
    int    rx_buffer_available() override;
    void   flushRx() override;
    size_t timedReadBytes(char* buffer, size_t length, TickType_t timeout);
    size_t timedReadBytes(uint8_t* buffer, size_t length, TickType_t timeout) { return timedReadBytes((char*)buffer, length, timeout); };
    bool   realtimeOkay(char c) override;
    bool   lineComplete(char* line, char c) override;
    Error  pollLine(char* line) override;

    // Configurable interface
    void group(Configuration::HandlerBase& handler) override;
};
extern USBCDCChannel CDCChannel;
