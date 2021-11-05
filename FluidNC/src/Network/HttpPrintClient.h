#pragma once

#include "../Config.h"

#ifdef INCLUDE_HTTP_PRINT_SERVICE

#    include <WiFi.h>

#    include "../Serial.h"

// Opens a connection and then ignores a header terminated by \r\n\r\n.
// The remainder of the data is output via single character read().
// isFinished() indicates that the client is closed and exhausted.

class HttpPrintClient : public Stream {
    enum State {
        READING_HEADER,
        READING_DATA,
        FINISHING,
        FINISHED,
    };

public:
    HttpPrintClient();
    HttpPrintClient(WiFiClient wifi_client);

    // All possible data has been read.
    bool is_done();
    bool is_aborted();

    // Stream interface.
    int    read() override;
    int    peek() override;
    void   flush() override;
    int    available() override;
    size_t write(uint8_t) override;

private:
    void set_state(State state);

    inline size_t is_content_exhausted() const { return _content_size == _content_read; }
    inline size_t is_data_exhausted() const { return _data_size == _data_read; }
    inline void   reset_data() {
        _data_read = 0;
        _data_size = 0;
    }

    enum State _state;
    WiFiClient _wifi_client;

    long   _content_read;
    long   _content_size;
    char   _data[128];
    size_t _data_read;
    size_t _data_size;
    bool   _aborted;
};

#endif  // INCLUDE_HTTP_PRINT_SERVICE
