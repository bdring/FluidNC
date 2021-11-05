#include "../Config.h"

#include "Network.h"
#include "../Logging.h"
#include "../Settings.h"

void Network::init() {
    // Do nothing.
    log_info("Network init");

#ifdef INCLUDE_HTTP_PRINT_SERVICE
    if (_http_print_server) {
        _http_print_server->init();
    }
#endif  // INCLUDE_HTTP_PRINT_SERVICE
}

void Network::handle() {
#ifdef INCLUDE_HTTP_PRINT_SERVICE
    if (_http_print_server) {
        _http_print_server->handle();
    }
#endif  // INCLUDE_HTTP_PRINT_SERVICE
}

const char* Network::name() const {
    return "network";
}

void Network::validate() const {
    // Do nothing.
}

void Network::group(Configuration::HandlerBase& handler) {
#ifdef INCLUDE_HTTP_PRINT_SERVICE
    handler.section("HttpPrintServer", _http_print_server);
#endif
}

void Network::afterParse() {
    // Do nothing.
}
