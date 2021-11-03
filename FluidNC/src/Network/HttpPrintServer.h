#pragma once

#include "../Config.h"

#ifdef INCLUDE_HTTP_PRINT_SERVICE

#    include <WiFi.h>

#    include "../Configuration/Configurable.h"
#    include "../Serial.h"
#    include "../Settings.h"
#    include "HttpPrintClient.h"

class HttpPrintServer : public Configuration::Configurable {
private:
    enum State {
        UNSTARTED,
        IDLE,
        PRINTING,
        STOPPED,
    };

public:
    HttpPrintServer();

    // The server will now start accepting connections.
    bool begin();

    // The server will accept no new connections.
    void stop();

    // Call this periodically to allow new connections to be established.
    void handle();

    // Configuration
    const char* name() const;
    void        validate() const override;
    void        afterParse() override;
    void        group(Configuration::HandlerBase& handler) override;
    void        init();

private:
    void setState(State state);

    enum State      _state;
    int             _port;
    WiFiServer      _server;
    HttpPrintClient _client;
    InputClient*    _input_client;  // Owned
    IntSetting*     _port_setting;  // NVS override setting
};

#endif  // INCLUDE_HTTP_PRINT_SERVICE
