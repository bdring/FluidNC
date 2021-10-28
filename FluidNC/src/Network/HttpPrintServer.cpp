#include "../Config.h"
#include "../Settings.h"

#include "HttpPrintServer.h"

HttpPrintServer::HttpPrintServer()
    : _state(STOPPED),
      _port(-1) {
}

bool HttpPrintServer::begin() {
    if (_state != UNSTARTED) {
        return false;
    }
    _server = WiFiServer(_port);
    _server.begin();
    _state = IDLE;
    return true;
}

void HttpPrintServer::stop() {
    if (_state == STOPPED) {
        return;
    }
    _server.stop();
    _state = STOPPED;
}

void HttpPrintServer::handle() {
    switch (_state) {
    case UNSTARTED:
        return;
    case IDLE:
        if (_server.hasClient()) {
            _client = HttpPrintClient(_server.available());
            _state = PRINTING;
            _input_client = register_client(&_client);
        }
        return;
    case PRINTING:
        if (_client.isFinished()) {
            unregister_client(_input_client);
            delete _input_client;
            _input_client = nullptr;
            _state = IDLE;
        }
        return;
    case STOPPED:
        return;
    }
}

void HttpPrintServer::init() {
    log_info("HttpPrintServer init");
    // Without a yaml configuration this will default to -1 which will disable the server.
    // A NVS setting will override the yaml configuration.
    _port_setting = new IntSetting(
        "HttpPrintServer Port",         // Description
        WEBSET,                         // Share NVS storage with WebUI
        WA,                             // Admin Write, User Read
        "NHPSP1",                       // Not quite sure how this gets used.
        "network/HttpPrintServer/port", // Which path takes precedence?
        _port,                          // Default to what we read from yaml.
        1,                              // Minimum value
        65001,                          // Maximum value
        NULL);                          // No checker.
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
