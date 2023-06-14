#pragma once

#include "../System.h"
#include "../Configuration/Configurable.h"
#include "../Configuration/GenericFactory.h"

namespace Listeners {
    class SysListener;
    using SysListenerList = std::vector<SysListener*>;

    class SysListener : public Configuration::Configurable {
    public:
        // Name is required for the configuration factory to work.
        virtual const char* name() const = 0;

        virtual void init() {}
        virtual void beforeVariableReset() {}
        virtual void afterVariableReset() {}
    };

    using SysListenerFactory = Configuration::GenericFactory<SysListener>;
}
