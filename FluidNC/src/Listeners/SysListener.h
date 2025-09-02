#pragma once

#include "System.h"
#include "Configuration/Configurable.h"
#include "Configuration/GenericFactory.h"

namespace Listeners {
    class SysListener;
    using SysListenerList = std::vector<SysListener*>;

    class SysListener : public Configuration::Configurable {
        const char* _name;

    public:
        SysListener(const char* name) : _name(name) {}
        const char*  name() { return _name; }
        virtual void init() {}
        virtual void beforeVariableReset() {}
        virtual void afterVariableReset() {}
    };

    using SysListenerFactory = Configuration::GenericFactory<SysListener>;
}
