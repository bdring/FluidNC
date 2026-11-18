#pragma once

#include <Arduino.h>
#include <HWCDC.h>

#include "Channel.h"
#include "lineedit.h"

class HWCDCChannel : public Channel {
private:
    static constexpr int RX_BUFFER_SIZE = 1040;

    Lineedit* _lineedit;
    HWCDC&    _cdc;

public:
    HWCDCChannel(bool addCR = false);

    void init() override;

    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buf, size_t len) override;

    int available() override;
    int peek() override;
    int read() override;
    int rx_buffer_available() override;
    void flushRx() override;

    size_t timedReadBytes(char* buffer, size_t length, TickType_t timeout) override;

    bool  realtimeOkay(char c) override;
    bool  lineComplete(char* line, char c) override;
    Error pollLine(char* line) override;
};

extern HWCDCChannel CDCChannel;
