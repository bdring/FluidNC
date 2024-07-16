#pragma once

#include "src/Config.h"

#include "src/Configuration/Configurable.h"
#include "src/Configuration/GenericFactory.h"

class Channel;
class Module;
class JSONencoder;

class Module : public Configuration::Configurable {
    const char* _name;

public:
    Module() : _name("noname") {}
    Module(const char* name) : _name(name) {}
    ~Module() = default;

    // Configuration system helpers:
    // Many modules do not have configuration items hence these null default
    // configuration helpers.  Configured modules can override them.
    void group(Configuration::HandlerBase& handler) override {}
    void afterParse() override {};

    const char* name() { return _name; };

    virtual void init() {}
    virtual void deinit() {}
    virtual void poll() {}

    virtual void status_report(Channel& out) {}
    virtual void build_info(Channel& out) {}
    virtual void wifi_stats(JSONencoder& j) {}
    virtual bool is_radio() { return false; }
};

using ModuleFactory = Configuration::GenericFactory<Module>;
inline std::vector<Module*>& Modules() {
    return ModuleFactory::objects();
}
