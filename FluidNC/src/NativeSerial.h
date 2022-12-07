#pragma once

#include "Config.h"

#include "Configuration/Configurable.h"
#include "UartTypes.h"

#include "lineedit.h"
#include "Channel.h"

#include <freertos/FreeRTOS.h>  // TickType_T

class NativeSerial : public Channel, public Configuration::Configurable {
private:
    Lineedit* _lineedit;

    // One character of pushback for implementing peek().
    // We cannot use the queue for this because the queue
    // is after the check for realtime characters, whereas
    // peek() deals with characters before realtime ones
    // are handled.
    int _pushback = -1;

public:
    // These are public so that validators from classes
    // that use Uart can check that the setup is suitable.
    // E.g. some uses require an RTS pin.

    // Configurable.  Uart0 uses a fixed configuration
    int baud = 115200;

    NativeSerial();

    void begin();
    void begin(unsigned long baud);

    int    available() override;
    int    read() override;
    int    read(TickType_t timeout);
    size_t timedReadBytes(char* buffer, size_t length, TickType_t timeout) override;
    size_t timedReadBytes(uint8_t* buffer, size_t length, TickType_t timeout) { return timedReadBytes((char*)buffer, length, timeout); };
    int    peek() override;

    size_t write(uint8_t data) override;
    size_t write(const uint8_t* buffer, size_t length) override;

    void flushRx() override;
    bool flushTxTimed(TickType_t ticks);

    bool isConnected();

    bool realtimeOkay(char c) override;
    bool lineComplete(char* line, char c) override;

    Channel* pollLine(char* line) override;

    int rx_buffer_available() override;

    // Configuration handlers:
    void validate() const override {}
    void afterParse() override {}

    void group(Configuration::HandlerBase& handler) override { handler.item("baud", baud, 2400, 4000000); }

    void config_message(const char* prefix, const char* usage);
};

extern NativeSerial Uart0;

extern void nativeSerialInit();
