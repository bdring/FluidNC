#pragma once

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

    // Configuration
    const char* name() const;
    void validate() const override;
    void afterParse() override;
    void group(Configuration::HandlerBase& handler) override;

    // Virtual base classes require a virtual destructor.
    virtual ~Network() {}

    private:
    HttpPrintServer* _http_print_server;
};
