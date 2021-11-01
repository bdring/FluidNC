#pragma once

#include <WebServer.h>

#include "../Configuration/Configurable.h"
#include "../Serial.h"
#include "../Settings.h"
#include "HttpPrintClient.h"

// The HttpPrintServer expects a text/plain encoded POST.
// It ignores the header and streams the content until EOF.

class HttpPrintServer : public Configuration::Configurable {
    private:
    enum State {
      UNSTARTED,
      IDLE,
      UPLOADING,
      STOPPED,
      ABORTED,
    };

    public:
    HttpPrintServer();
    virtual ~HttpPrintServer();

    void close_print_client();
    void close_web_server();

    // The server will now start accepting connections.
    bool begin();

    // The server will accept no new connections.
    void stop();

    // Call this periodically to allow new connections to be established.
    void handle();

    // Called for file upload.
    void print_form();
    void print_upload();
    void print_response();

    void init();

    // Configuration
    const char* name() const;
    void validate() const override;
    void afterParse() override;
    void group(Configuration::HandlerBase& handler) override;

    private:
    void set_state(State state);

    enum State _state;
    int _port;
    WebServer* _web_server;
    HttpPrintClient* _print_client;
    InputClient* _input_client; // Owned
    IntSetting* _port_setting;  // NVS override setting

    static const char* _state_name[5];
};
