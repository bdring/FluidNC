#include "Network.h"
#include "../Logging.h"
#include "../Settings.h"

void Network::init() {
  if (_http_print_server) {
    _http_print_server->init();
  }
}

void Network::handle() {
    if (_http_print_server) {
        _http_print_server->handle();
    }
}

const char* Network::name() const {
    return "network";
}

void Network::validate() const {
  // Do nothing.
}

void Network::group(Configuration::HandlerBase& handler) {
    handler.section("HttpPrintServer", _http_print_server);
}

void Network::afterParse() {
  // Do nothing.
}
