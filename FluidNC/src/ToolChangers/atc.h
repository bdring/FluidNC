#pragma once

#include "src/Config.h"

#include "src/Configuration/Configurable.h"

#include "src/Channel.h"
#include "src/Module.h"
namespace ATCs {
    class ATC : public Configuration::Configurable {
    protected:
        const char* _name;
        uint32_t    _last_tool = 0;
        bool        _error     = false;

    public:
        ATC(const char* name) : _name(name) {}

        ATC(const ATC&)                    = delete;
        ATC(ATC&&)                         = delete;
        ATC&     operator=(const ATC&)     = delete;
        ATC&     operator=(ATC&&)          = delete;

        virtual ~ATC() = default;

        const char* name() { return _name; }

        virtual void init() = 0;

        virtual void probe_notification();
        virtual void tool_change(uint8_t value, bool pre_select);

        ATC* __atc;

        // Configuration handlers:
        void validate() override {}
        void afterParse() override {};
        void group(Configuration::HandlerBase& handler) override {}
    };

    using ATCFactory = Configuration::GenericFactory<ATC>;
}
extern ATCs::ATC* atc;
