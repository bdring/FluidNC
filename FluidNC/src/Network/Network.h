#pragma once

#include "../Config.h"

#include "../Configuration/Configurable.h"

#include "HttpPrintServer.h"

class Network : public Configuration::Configurable {
public:
    Network() = default;

    Network(const Network&) = delete;
    Network(Network&&)      = delete;
    Network& operator=(const Network&) = delete;
    Network& operator=(Network&&) = delete;

    void init();
    void handle();

    // Configuration
    const char* name() const;
    void        validate() const override;
    void        afterParse() override;
    void        group(Configuration::HandlerBase& handler) override;

    // Virtual base classes require a virtual destructor.
    virtual ~Network() {}

private:
#ifdef INCLUDE_HTTP_PRINT_SERVICE
    HttpPrintServer* _http_print_server;
#endif
};
