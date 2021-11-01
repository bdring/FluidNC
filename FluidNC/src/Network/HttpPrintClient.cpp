#include <WebServer.h>

#include "HttpPrintServer.h"

#include "../Config.h"

// Opens a connection and then ignores a header terminated by \r\n\r\n.
// The remainder of the data is output via single character read().
// isFinished() indicates that the client is closed and exhausted.

HttpPrintClient::HttpPrintClient(WebServer* web_server)
    : _state(IDLE),
      _web_server(web_server),
      _uploaded_data_read(0),
      _uploaded_data_size(0) {
}

void HttpPrintClient::set_state(State state) {
    if (_state != state) {
        switch (state) {
            case IDLE:
                log_info("HttpPrintClient: IDLE");
                break;
            case PRINTING:
                log_info("HttpPrintClient: PRINTING");
                break;
            case FINISHED:
                log_info("HttpPrintClient: FINISHED");
                break;
            case ABORTED:
                log_info("HttpPrintClient: ABORTED");
                break;
            default:
                log_info("HttpPrintClient: <Unknown State>");
                break;
        }
    }
    _state = state;
}

bool HttpPrintClient::isDone() {
    return _state == FINISHED || _state == ABORTED;
}

bool HttpPrintClient::isAborted() {
    return _state == ABORTED;
}

void HttpPrintClient::handle_upload() {
    HTTPUpload& upload = _web_server->upload();
    log_info("HttpPrintClient: read=" << _uploaded_data_read << " size=" << _uploaded_data_size);
    switch (upload.status) {
        case UPLOAD_FILE_START:
            log_info("HttpPrintClient: UPLOAD_FILE_START");
            switch (_state) {
                case PRINTING:
                case FINISHING:
                case FINISHED:
                case ABORTED:
                    log_info("HttpPrintClient: UPLOAD_FILE_START while " << _state_name[_state]);
                    set_state(ABORTED);
                    break;
                case IDLE:
                    set_state(PRINTING);
                    break;
            }
            break;
        case UPLOAD_FILE_WRITE:
            log_info("HttpPrintClient: UPLOAD_FILE_WRITE data=" << String((char *)upload.buf).substring(0, 20));
            switch (_state) {
                case IDLE:
                case FINISHING:
                case FINISHED:
                case ABORTED:
                    log_info("HttpPrintClient: UPLOAD_FILE_WRITE while " << _state_name[_state]);
                    set_state(ABORTED);
                    break;
                case PRINTING:
                    if (_uploaded_data_read != _uploaded_data_size) {
                        log_info("HttpPrintClient: Received data while buffer not empty.");
                        set_state(ABORTED);
                        break;
                    }
                    // Unfortunately as soon as we return the upload buffer
                    // will be invalidated, so we need to stash the data.
                    //
                    // If we used WiFiClient directly this could be avoided
                    // but then we'd need to re-implement all of the upload
                    // protocol.
                    memcpy(_uploaded_data, upload.buf, upload.currentSize);
                    _uploaded_data_read = 0;
                    _uploaded_data_size = upload.currentSize;
                    break;
            }
            break;
        case UPLOAD_FILE_END:
            log_info("HttpPrintClient: UPLOAD_FILE_END");
            switch (_state) {
                case IDLE:
                case FINISHING:
                case FINISHED:
                case ABORTED:
                    log_info("HttpPrintClient: UPLOAD_FILE_END while " << _state_name[_state]);
                    set_state(ABORTED);
                    break;
                case PRINTING:
                    if (_uploaded_data_read < _uploaded_data_size) {
                        set_state(FINISHING);
                    } else {
                        set_state(FINISHED);
                    }
                    break;
            }
            break;
        case UPLOAD_FILE_ABORTED:
            log_info("HttpPrintClient: UPLOAD_FILE_ABORTED while " << _state_name[_state]);
            set_state(ABORTED);
            break;
    }
}

int HttpPrintClient::peek() {
    HTTPUpload& upload = _web_server->upload();
    if (_state == PRINTING &&_uploaded_data_read < _uploaded_data_size) {
        return _uploaded_data[_uploaded_data_read];
    }
    // Permit the web server to advance.
    log_info("HttpPrintClient::peek advance");
    _web_server->handleClient();
    // Delegate to the next cycle.
    return -1;
}

int HttpPrintClient::read() {
    int code = peek();
    if (code != -1) {
        _uploaded_data_read++;
        log_info("HttpPrintClient::read read=" << _uploaded_data_read << " size=" << _uploaded_data_size);
        if (_state == FINISHING && _uploaded_data_read == _uploaded_data_size) {
            set_state(FINISHED);
        }
    }
    return code;
}

void HttpPrintClient::flush() {
    // Do nothing.
}

int HttpPrintClient::available() {
    if (_state != PRINTING) {
        // Permit the web server to advance.
        log_info("HttpPrintClient::available advance");
        _web_server->handleClient();
        return 0;
    }
    return _uploaded_data_size - _uploaded_data_read;
}

size_t HttpPrintClient::write(uint8_t) {
    return 0;
}

const char* HttpPrintClient::_state_name[5] = {
    "IDLE",
    "PRINTING",
    "FINISHING",
    "FINISHED",
    "ABORTED",
};
