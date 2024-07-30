#pragma once

#include "src/Config.h"

#include "src/Configuration/Configurable.h"

#include "src/Channel.h"
#include "src/Module.h"

class ATC : public Module {
public:
    ATC(const char* name) : Module(name) {}

private:
    // config items
    uint32_t _last_tool = 0;
    bool     _error     = false;

public:
    ATC() : Module("atc") {}

    ATC(const ATC&)            = delete;
    ATC(ATC&&)                 = delete;
    ATC& operator=(const ATC&) = delete;
    ATC& operator=(ATC&&)      = delete;

    virtual ~ATC() = default;

    virtual void init() = 0;

    virtual void probe_notification();
    virtual void tool_change(uint8_t value, bool pre_select);

    ATC* __atc;

    // Configuration handlers:
    void validate() override {}
    void afterParse() override;
    void group(Configuration::HandlerBase& handler) override {}
};
