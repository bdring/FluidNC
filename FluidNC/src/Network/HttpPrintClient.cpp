#include "../Config.h"

#include "HttpPrintServer.h"

namespace {
    const char* _state_name[4] = {
        "READING_HEADER",
        "READING_DATA",
        "FINISHED",
        "ABORTED",
    };

    const char content_length[] = "Content_length:";
    const char header_delimiter[] = "\r\n";
}

HttpPrintClient::HttpPrintClient(WiFiClient wifi_client)
    : _state(READING_HEADER),
      _wifi_client(wifi_client),
      _content_read(0),
      _content_size(0),
      _data_read(0),
      _data_size(0) {
}

HttpPrintClient::HttpPrintClient()
    : _state(READING_HEADER) {
}

bool HttpPrintClient::is_done() {
    return _state == FINISHED || _state == ABORTED;
}

bool HttpPrintClient::is_aborted() {
    return _state == ABORTED;
}

void HttpPrintClient::set_state(State state) {
    if (_state != state) {
        log_info("HttpPrintClient: " << _state_name[state]);
        _state = state;
    }
}

// This is sufficient to drive the client due to how pollClients() just calls
// read().
int HttpPrintClient::read() {
    if (_wifi_client.available() == 0) {
        if (!_wifi_client.connected()) {
            // There is nothing to read and we are not connected.
            _wifi_client.stop();
            set_state(ABORTED);
        }
        return -1;
    }
    // We need to read the Content-length since we can't detect a half-closed socket.
    switch (_state) {
        case READING_HEADER: {
            // POST /test HTTP/1.1
            // Host: foo.example
            // Content-Type: application/x-www-form-urlencoded
            // Content-Length: 27

            if (_data_size == sizeof _data) {
                // The header line was too long, throw away the start.
                _data_size = 0;
            }

            int code = _wifi_client.read();
            if (code == -1) {
                return -1;
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
                _data_size = 0;
            }
            return -1;
        }
        case READING_DATA: {
            int code = peek();
            if (code != -1) {
                _data_read++;
                _content_read++;
                if (_content_read == _content_size) {
                    set_state(FINISHED);
                }
            }
            return code;
        }
        case FINISHED:
            return -1;
        case ABORTED:
            return -1;
    }
    // Unreachable
    return -1;
}

int HttpPrintClient::peek() {
    if (_state != READING_DATA) {
        return -1;
    }
    if (_data_read == _data_size) {
        if (!_wifi_client.available()) {
            if (!_wifi_client.connected()) {
                // There is nothing to read and we are not connected.
                _wifi_client.stop();
                set_state(ABORTED);
            }
            return -1;
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
