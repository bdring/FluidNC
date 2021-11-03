#include "../Config.h"

#ifdef INCLUDE_HTTP_PRINT_SERVICE

#    include <lwip/sockets.h>

// A workaround for an issue in lwip/sockets.h
// See https://github.com/espressif/arduino-esp32/issues/4073
#    undef IPADDR_NONE

#    include "HttpPrintServer.h"

// We expect input like so:
//
// POST /test HTTP/1.1
// Host: foo.example
// Content-Type: application/x-www-form-urlencoded
// Content-Length: 7
//
// G0 Z1
//
// We ignore the header except for Content-Length.

#    define RETRY -1

namespace {
    const char* _state_name[4] = {
        "READING_HEADER",
        "READING_DATA",
        "FINISHED",
        "ABORTED",
    };

    const char content_length[] = "Content-Length:";

    const char header_delimiter[] = "\r\n";

    // The print completed successfully.
    const char ok_200[] = "HTTP/1.1 200 OK\r\n"
                          "Content-Length: 0\r\n"
                          "Access-Control-Allow-Origin: *\r\n"
                          "\r\n";

    // Something went wrong, but the user can correct the problem and try again.
    const char conflict_207[] = "HTTP/1.1 207 CONFLICT\r\n"
                                "Content-Length: 0\r\n"
                                "Access-Control-Allow-Origin: *\r\n"
                                "\r\n";
}

HttpPrintClient::HttpPrintClient(WiFiClient wifi_client) :
    _state(READING_HEADER), _wifi_client(wifi_client), _content_read(0), _content_size(0), _data_read(0), _data_size(0) {}

HttpPrintClient::HttpPrintClient() : _state(READING_HEADER) {}

bool HttpPrintClient::is_done() {
    return _state == FINISHED || _state == ABORTED;
}

bool HttpPrintClient::is_aborted() {
    return _state == ABORTED;
}

void HttpPrintClient::set_state(State state) {
    if (_state != state) {
        // Show the state changes so we can see what's happening via other
        // clients.
        log_info("HttpPrintClient: " << _state_name[state]);
        _state = state;

        switch (_state) {
            case READING_HEADER:
            case READING_DATA:
                break;
            case FINISHED: {
                size_t nw = _wifi_client.write(ok_200, sizeof ok_200 - 1);
                log_info("nw= " << String(nw));
                shutdown(_wifi_client.fd(), SHUT_RDWR);
                break;
            }
            case ABORTED: {
                size_t nw = _wifi_client.write(conflict_207, sizeof conflict_207 - 1);
                log_info("nw= " << String(nw));
                shutdown(_wifi_client.fd(), SHUT_RDWR);
                break;
            }
        }
    }
}

// This is sufficient to drive the client due to how pollClients() just calls
// read().
int HttpPrintClient::read() {
    // We need to read the Content-length since we can't detect a half-closed socket.
    switch (_state) {
        case FINISHED:
        case ABORTED:
            return RETRY;
        case READING_HEADER: {
            if (is_data_exhausted()) {
                // The header line was too long, throw away the start.
                reset_data();
            }

            int code = _wifi_client.read();
            if (code == RETRY) {
                return code;
            }

            _data[_data_size++] = code;

            if (code == '\n') {
                // We have a complete line.
                if (strncmp(_data, content_length, sizeof content_length - 1) == 0) {
                    // Content-Length: 1234
                    _content_size = atol(_data + sizeof content_length);
                } else if (strncmp(_data, header_delimiter, sizeof header_delimiter - 1) == 0) {
                    // An empty line to terminate the header.
                    set_state(READING_DATA);
                }
                reset_data();
            }
            return RETRY;
        }
        case READING_DATA: {
            int code = peek();
            if (code != RETRY) {
                _data_read++;
                _content_read++;
                if (is_content_exhausted()) {
                    set_state(FINISHED);
                }
            }
            return code;
        }
    }
    // Unreachable
    return RETRY;
}

int HttpPrintClient::peek() {
    if (_state != READING_DATA) {
        return RETRY;
    }
    if (is_data_exhausted()) {
        if (!_wifi_client.available()) {
            if (!_wifi_client.connected()) {
                // There is nothing to read and we are not connected.
                _wifi_client.stop();
                set_state(ABORTED);
            }
            return RETRY;
        }
        _data_read = 0;
        _data_size = _wifi_client.readBytes(_data, sizeof _data);
    }
    return _data[_data_read];
}

void HttpPrintClient::flush() {
    if (_state != READING_DATA) {
        return;
    }
    _wifi_client.flush();
}

int HttpPrintClient::available() {
    if (_state != READING_DATA) {
        return 0;
    }
    return _wifi_client.available();
}

size_t HttpPrintClient::write(uint8_t) {
    return 0;
}

#endif  // INCLUDE_HTTP_PRINT_SERVICE
