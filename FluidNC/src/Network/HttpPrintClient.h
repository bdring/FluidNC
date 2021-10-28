#pragma once

#include <WiFi.h>

#include "../Serial.h"

// Opens a connection and then ignores a header terminated by \r\n\r\n.
// The remainder of the data is output via single character read().
// isFinished() indicates that the client is closed and exhausted.

class HttpPrintClient : public Stream {
    enum State {
        READING_HEADER,
        READING_HEADER_1, // \r
        READING_HEADER_2, // \r\n
        READING_HEADER_3, // \r\n\r
        READING_DATA,
        DISCONNECTED,
    };

    public:
    HttpPrintClient();
    HttpPrintClient(WiFiClient wifi_client);

    // All possible data has been read.
    bool isFinished();

    // Stream interface.
    int read() override;
    int peek() override;
    void flush() override;
    int available() override;
    size_t write(uint8_t) override;

    private:
    enum State _state;
    WiFiClient _wifi_client;
};
