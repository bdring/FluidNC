#pragma once

#include <WebServer.h>

#include "../Serial.h"

// Opens a connection and then ignores a header terminated by \r\n\r\n.
// The remainder of the data is output via single character read().
// isFinished() indicates that the client is closed and exhausted.

class HttpPrintClient : public Stream {
    enum State {
        IDLE,
        PRINTING,
        FINISHING,
        FINISHED,
        ABORTED,
    };

    public:
    HttpPrintClient(WebServer* server);

    // Advise of an upload state change.
    void handle_upload();

    bool isAborted();
    bool isDone();

    // Stream interface.
    int read() override;
    int peek() override;
    void flush() override;
    int available() override;
    size_t write(uint8_t) override;

    private:
    void set_state(State state);

    enum State _state;
    WebServer* _web_server;  // Not owned.
    size_t _uploaded_data_read;
    size_t _uploaded_data_size;
    uint8_t _uploaded_data[HTTP_UPLOAD_BUFLEN];

    static const char* _state_name[5];
};
