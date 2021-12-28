// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"
#include "../Assert.h"
#include "../Configuration/GenericFactory.h"
#include "../Configuration/HandlerBase.h"
#include "../Configuration/Configurable.h"
#include "../Pin.h"

#include <cstdint>     // int32_t
#include <WString.h>   // String
#include <esp_attr.h>  // IRAM_ATTR

namespace Probes {
    using TripProbe = void (*)(int32_t tickDelta);

    class ProbeDriver : public Configuration::Configurable {
    protected:
        String     probeName_;
        TripProbe* callback_ = nullptr;

    public:
        ProbeDriver()                     = default;
        ProbeDriver(const ProbeDriver& o) = delete;
        ProbeDriver(ProbeDriver&& o)      = delete;

        // ISR function. Should not be used directly.
        void IRAM_ATTR tripISR(int32_t tickDelta);

        String probeName() const;

        virtual const char* name() const = 0;

        virtual void init(TripProbe* callback);
        virtual void start_cycle() = 0;
        virtual void stop_cycle()  = 0;
        virtual bool is_tripped()  = 0;

        void IRAM_ATTR trip(int32_t tickDelta);

        virtual ~ProbeDriver();
    };

    using ProbeFactory = Configuration::GenericFactory<ProbeDriver>;
}
