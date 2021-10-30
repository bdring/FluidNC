#include "../Config.h"

#include "HttpPrintServer.h"

// Opens a connection and then ignores a header terminated by \r\n\r\n.
// The remainder of the data is output via single character read().
// isFinished() indicates that the client is closed and exhausted.

HttpPrintClient::HttpPrintClient(WiFiClient wifi_client)
    : _state(READING_HEADER),
      _wifi_client(wifi_client) {
}

HttpPrintClient::HttpPrintClient()
    : _state(DISCONNECTED) {
}

bool HttpPrintClient::isFinished() {
    return _state == DISCONNECTED;
}

void HttpPrintClient::setState(State state) {
    if (_state != state) {
        switch (state) {
        case READING_HEADER:
            log_info("HttpPrintClient: READING_HEADER");
            break;
        case READING_HEADER_1:
            log_info("HttpPrintClient: READING_HEADER_1");
            break;
        case READING_HEADER_2:
            log_info("HttpPrintClient: READING_HEADER_2");
            break;
        case READING_HEADER_3:
            log_info("HttpPrintClient: READING_HEADER_3");
            break;
        case READING_DATA:
            log_info("HttpPrintClient: READING_DATA");
            break;
        case DISCONNECTED:
            log_info("HttpPrintClient: DISCONNECTED");
            break;
        default:
            log_info("HttpPrintClient: <Unknown State>");
            break;
        }
    }
    _state = state;
}

// This is sufficient to drive the client due to how pollClients() just calls
// read().
int HttpPrintClient::read() {
    if (_wifi_client.available() == 0) {
        if (!_wifi_client.connected()) {
            // There is nothing to read and we are not connected.
            _wifi_client.stop();
            setState(DISCONNECTED);
        }
        return -1;
    }
    // We need to read the Content-length since we can't detect a half-closed socket.
    switch (_state) {
    case READING_HEADER:
        if (_wifi_client.read() == '\r') {
            setState(READING_HEADER_1);
        } else {
            setState(READING_HEADER);
        }
        return -1;
    case READING_HEADER_1:  // we have read \r
        if (_wifi_client.read() == '\n') {
            setState(READING_HEADER_2);
        } else {
            setState(READING_HEADER);
        }
        return -1;
    case READING_HEADER_2: // we have read \r\n
        if (_wifi_client.read() == '\r') {
            setState(READING_HEADER_3);
        } else {
            setState(READING_HEADER);
        }
        return -1;
    case READING_HEADER_3: // we have read \r\n\r
        if (_wifi_client.read() == '\n') {
            setState(READING_DATA);
        } else {
            setState(READING_HEADER);
        }
        return -1;
    case READING_DATA: {
        return _wifi_client.read();
    }
    case DISCONNECTED:
        return -1;
    }
    // Unreachable
    return -1;
}

int HttpPrintClient::peek() {
  if (_state != READING_DATA) {
      return -1;
  }
  return _wifi_client.peek();
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
