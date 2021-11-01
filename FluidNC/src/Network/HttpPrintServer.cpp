#include <WebServer.h>

#include "../Config.h"
#include "../Protocol.h"
#include "../Settings.h"

#include "HttpPrintServer.h"

HttpPrintServer::HttpPrintServer()
    : _state(UNSTARTED),
      _port(-1),
      _web_server(nullptr),
      _print_client(nullptr),
      _input_client(nullptr) {
}

HttpPrintServer::~HttpPrintServer() {
    close_print_client();
    close_web_server();
}

void HttpPrintServer::close_print_client() {
    if (_input_client) {
      unregister_client(_input_client);
      delete _input_client;
      _input_client = nullptr;
    }
    if (_print_client) {
      delete _print_client;
      _print_client = nullptr;
    }
}

void HttpPrintServer::close_web_server() {
    if (_web_server) {
        delete _web_server;
        _web_server = nullptr;
    }
}

bool HttpPrintServer::begin() {
    if (_state != UNSTARTED || _port == -1) {
        return false;
    }
    _web_server = new WebServer(_port);
    _web_server->on("/", [&]() { print_form(); });
    _web_server->on("/print", HTTP_POST, [&]() { print_response(); }, [&]() { print_upload(); });
    _web_server->begin();
    set_state(IDLE);
    return true;
}

void HttpPrintServer::print_form() {
  _web_server->send(
      200,
      "text/html",
      R"form(
<html>
 <body>
  <form action='/print' method='post' enctype='multipart/form-data'>
   <input type='file' name='gcode'>
   <br>
   <br>
   <button type='submit'>
    Print GCode
   </button>
   <br>
  </form>
 </body>
</html>

)form");
}

void HttpPrintServer::print_upload() {
    switch (_state) {
        case ABORTED:
        case UNSTARTED:
        case STOPPED:
            // These should be impossible.
            log_info("HttpPrintServer: Received upload while " << _state_name[_state]);
            set_state(ABORTED);
            return;
        case IDLE:
            log_info("Setting state to UPLOADING");
            set_state(UPLOADING);
            log_info("State is " << _state_name[_state]);
            _print_client = new HttpPrintClient(_web_server);
            _input_client = register_client(_print_client);
            // Fall through.
        case UPLOADING:
            _print_client->handle_upload();
            return;
    }
}

void HttpPrintServer::print_response() {
    switch (_state) {
        case UPLOADING:
            if (_print_client->isAborted()) {
                log_info("HttpPrintServer: Upload aborted");
                _web_server->send(409);
            } else {
                log_info("HttpPrintServer: Upload completed");
                _web_server->send(200);
            }
            return;
        default:
            return;
    }
}


void HttpPrintServer::stop() {
    if (_state == STOPPED) {
        return;
    }
    _web_server->stop();
    close_print_client();
    close_web_server();
    set_state(STOPPED);
}

void HttpPrintServer::handle() {
    switch (_state) {
    case ABORTED:
    case STOPPED:
    case UNSTARTED:
        return;
    case IDLE:
        log_info("HttpPrintServer::handle advance");
        _web_server->handleClient();
        return;
    case UPLOADING:
        _print_client->handle_upload();
        if (_print_client->isDone()) {
            close_print_client();
            set_state(IDLE);
        }
        return;
    }
}

void HttpPrintServer::set_state(State state) {
    if (_state != state) {
        log_info("HttpPrintServer: " << _state_name[_state]);
        if (_state == ABORTED) {
            // Place a hold for user attention.
            rtFeedHold = true;
        }
    }
    _state = state;
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

const char* HttpPrintServer::_state_name[5] = {
    "UNSTARTED",
    "IDLE",
    "UPLOADING",
    "STOPPED",
    "ABORTED",
};
