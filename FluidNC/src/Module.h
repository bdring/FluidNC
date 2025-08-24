// Copyright (c) 2024 Mitch Bradley All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
A Module is a source file that can be included or excluded from the FluidNC build
simply by adding or removing the filename from build_src_filter in platformio.ini
It is not necessary to guard the file contents with #ifdef ENABLE_NAME .. #endif

Module symbols, and the name of the module itself, are generally
not visible to or referenced from outside code, except for the few
methods of the module abstract interface.  The module's
functionality is invoked in various places in FluidNC with

    for (auto const& module : Modules()) {
        module->METHOD();
    }

which calls METHOD() on all of the registered objects.

Each module is registered with, for example,

    ModuleFactory::InstanceBuilder<OLED> oled_module __attribute__((init_priority(104))) ("oled");

That creates an instance of the module's derived class and arranges for it to be configured if
necessary.  The init_priority value permits modules to be initialized in a defined order, for
cases where one module depends on another.  For example, the TelnetServer module requires that
the WifiConfig module be initialized first.  Lower numbers are initialized before higher numbers.
If two modules have the same number, the order among them is undefined.

The ConfigurableModule class derives from Configurable, so a module can define its own
configuration items by overriding the group() method.  A module that needs no configuration items
should derive from the Module class.

ConfigurableModule methods:
   void init()
       FluidNC calls all the init methods at startup, to prepare the modules for use
   void deinit()
       The deinit method disables the module.  FluidNC does not call the deinit()
       methods.  It is for completeness and possible future use.

Module methods:
   void init()
       FluidNC calls all the init methods at startup, to prepare the modules for use
   void deinit()
       The deinit method disables the module.  FluidNC does not call the deinit()
       methods.  It is for completeness and possible future use.
   void poll()
       FluidNC calls all the poll() methods when waiting for input.  If the module
       needs to be called periodically, it can implement this.
   void status_report(Channel& out)
       FluidNC calls all the status_report() methods when preparing a status report
       (the response to a ? realtime command, or with auto-reporting).  If the
       module needs to add information to the report, it can implement this.
   void build_info(Channel& out)
       FluidNC calls all the build_info() methods when responding to $I.  If the
       module needs to add information to the report, it can implement this.
   void wifi_stats(JSONencoder& j)
       FluidNC calls all the wifi_stats() methods when responding to [ESP420] from
       WebUI. If the module needs to add information to the report, it can implement this.
    bool is_radio()
       Returns true if the module is for a radio like Bluetooth or WiFi.  This is
       used to populate the "R" field in the Grbl signon message.
*/
#pragma once

#include "Config.h"

#include "Configuration/Configurable.h"
#include "Configuration/GenericFactory.h"

class Channel;
class Module;
class JSONencoder;

class Module {
    const char* _name;

public:
    Module() : _name("noname") {}
    Module(const char* name) : _name(name) {}
    ~Module() = default;

    const char* name() { return _name; };

    virtual void init() {}
    virtual void deinit() {}
    virtual void poll() {}

    virtual void status_report(Channel& out) {}
    virtual void build_info(Channel& out) {}
    virtual void wifi_stats(JSONencoder& j) {}
    virtual bool is_radio() { return false; }
};

class ConfigurableModule : public Configuration::Configurable {
    const char* _name;

public:
    ConfigurableModule(const char* name) : _name(name) {}
    ~ConfigurableModule() = default;

    const char*  name() { return _name; };
    virtual void init() {}
    virtual void deinit() {}
};

using ModuleFactory = Configuration::GenericFactory<Module>;
inline std::vector<Module*>& Modules() {
    return ModuleFactory::objects();
}

using ConfigurableModuleFactory = Configuration::GenericFactory<ConfigurableModule>;
inline std::vector<ConfigurableModule*>& ConfigurableModules() {
    return ConfigurableModuleFactory::objects();
}
