// Copyright (c) 2024 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Config.h"

#include "Configuration/Configurable.h"

#include "Channel.h"
#include "Module.h"
namespace ATCs {
    class ATC : public Configuration::Configurable {
    protected:
        const char* _name;
        uint32_t    _last_tool = 0;
        bool        _error     = false;

    public:
        ATC(const char* name) : _name(name) {}

        ATC(const ATC&)            = delete;
        ATC(ATC&&)                 = delete;
        ATC& operator=(const ATC&) = delete;
        ATC& operator=(ATC&&)      = delete;

        virtual ~ATC() = default;

        const char* name() { return _name; }

        virtual void init() = 0;

        virtual void probe_notification() = 0;
        // channel: the channel whose command line (M6, M61) triggered this
        // call, so a subclass that runs a macro can defer that command's ack
        // to the macro's completion instead of replying to it immediately
        // (see Macro::run()'s defer_ack argument, FluidNC issue #1862).
        // Return true if a macro/job was actually started (its ack is now
        // deferred), false otherwise -- the caller must know which, to
        // decide whether to return Error::Deferred.
        virtual bool tool_change(tool_t value, bool pre_select, bool set_tool, Channel* channel) = 0;

        ATC* _atc;

        // Configuration handlers:
        void validate() override {}
        void afterParse() override {};
        void group(Configuration::HandlerBase& handler) override {}
    };

    using ATCFactory = Configuration::GenericFactory<ATC>;
}
