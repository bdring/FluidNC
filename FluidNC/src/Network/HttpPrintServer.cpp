#include "../Config.h"

#ifdef INCLUDE_HTTP_PRINT_SERVICE

#    include "../Protocol.h"
#    include "../Settings.h"

#    include "HttpPrintServer.h"

namespace {
    const char* _state_name[4] = {
        "UNSTARTED",
        "IDLE",
        "PRINTING",
        "STOPPED",
    };
}

HttpPrintServer::HttpPrintServer() : _state(UNSTARTED), _port(-1) {}

bool HttpPrintServer::begin() {
    if (_state != UNSTARTED || _port == -1) {
        return false;
    }
    _server = WiFiServer(_port);
    _server.begin();
    setState(IDLE);
    return true;
}

void HttpPrintServer::stop() {
    if (_state == STOPPED) {
        return;
    }
    _server.stop();
    setState(STOPPED);
}

void HttpPrintServer::handle() {
    switch (_state) {
        case UNSTARTED:
        case STOPPED:
            return;
        case IDLE:
            if (_server.hasClient()) {
                _client = HttpPrintClient(_server.available());
                setState(PRINTING);
                _input_client = register_client(&_client);
            }
            return;
        case PRINTING:
            if (_client.is_done()) {
                // Remove the client from the polling cycle.
                unregister_client(_input_client);
                delete _input_client;
                _input_client = nullptr;
                if (_client.is_aborted()) {
                    log_info("HttpPrintServer: Setting HOLD due to aborted upload");
                    rtFeedHold = true;
                }
                setState(IDLE);
            }
            return;
    }
}

void HttpPrintServer::setState(State state) {
    if (_state != state) {
        log_info("HttpPrintServer: " << _state_name[state]);
        _state = state;
    }
}

void HttpPrintServer::init() {
    log_info("HttpPrintServer init");
    // Without a yaml configuration this will default to -1 which will disable the server.
    // A NVS setting will override the yaml configuration.
    _port_setting = new IntSetting("HttpPrintServer Port",          // Description
                                   WEBSET,                          // Share NVS storage with WebUI
                                   WA,                              // Admin Write, User Read
                                   "NHPSP1",                        // Not quite sure how this gets used.
                                   "network/HttpPrintServer/port",  // Which path takes precedence?
                                   _port,                           // Default to what we read from yaml.
                                   1,                               // Minimum value
                                   65001,                           // Maximum value
                                   NULL);                           // No checker.
    // And extract the value.
    // I guess the server will need a restart for update.
    _port = _port_setting->get();
    // And start the server.
    begin();
    log_info("HttpPrintServer port=" << _port);
}

const char* HttpPrintServer::name() const {
    return "HttpPrintServer";
}

void HttpPrintServer::validate() const {
    // Do nothing.
}

void HttpPrintServer::group(Configuration::HandlerBase& handler) {
    handler.item("port", _port);
}

void HttpPrintServer::afterParse() {
    // Do nothing.
}

#endif  // INCLUDE_HTTP_PRINT_SERVICE
