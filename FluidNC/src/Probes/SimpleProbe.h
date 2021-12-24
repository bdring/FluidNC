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
    using TripProbe = void IRAM_ATTR (*)(int32_t tickDelta);

    class ProbeDriver : public Configuration::Configurable {
    protected:
        String     probeName_;
        TripProbe* callback_ = nullptr;

        void IRAM_ATTR tripISR() { callback_(0); }

    public:
        ProbeDriver()                     = default;
        ProbeDriver(const ProbeDriver& o) = delete;
        ProbeDriver(ProbeDriver&& o)      = delete;

        String probeName() const { return probeName_; }

        virtual const char* name() const = 0;

        virtual void init(TripProbe* callback) { callback_ = callback; }
        virtual void start_cycle() = 0;
        virtual void stop_cycle()  = 0;
        virtual bool is_tripped()  = 0;

        void IRAM_ATTR trip(int32_t tickDelta) { callback_(tickDelta); }

        virtual ~ProbeDriver() {}
    };

    using ProbeFactory = Configuration::GenericFactory<ProbeDriver>;

    class SimpleProbe : public ProbeDriver {
        Pin _probePin;

    public:
        const char* name() const override { return "simple_probe"; }

        void validate() const override {}
        void group(Configuration::HandlerBase& handler) override {
            handler.item("pin", _probePin);
            handler.item("check_mode_start", _check_mode_start);
        }

        void init(TripProbe* callback) {
            ProbeDriver::init(callback);
            _probePin.attachInterrupt<SimpleProbe, &SimpleProbe::tripISR>(this, CHANGE);
        }
        void start_cycle() {}
        void stop_cycle() {}
        bool is_tripped() { return _probePin.read(); }

        ~SimpleProbe() {}
    };

    class ToolSetter : public SimpleProbe {
        // TODO: Soft limits; we need the tool setter height to calculate the soft limit
        float _height;

    public:
        const char* name() const override { return "tool_setter"; }

        // Configuration handlers:
        void validate() const override { SimpleProbe::validate(handler); }
        void group(Configuration::HandlerBase& handler) override {
            SimpleProbe::group(handler);
            handler.item("tool_setter_height", _height);
        }

        void init(TripProbe* callback) { SimpleProbe::init(callback); }
        void start_cycle() {
            // TODO FIXME: reset soft limits to original
        }
        void stop_cycle() {
            // TODO FIXME: set soft limits with height
        }

        bool is_tripped() { return _probePin.read(); }

        ~ToolSetter() {}
    };

    class BLTouch : public SimpleProbe {
        // servo pwm config

    public:
        const char* name() const override { return "bl_touch"; }

        // Configuration handlers:
        void validate() const override { SimpleProbe::validate(handler); }
        void group(Configuration::HandlerBase& handler) override {
            SimpleProbe::group(handler);
            // pwm config
        }

        void init(TripProbe* callback) {
            SimpleProbe::init(callback);

            // init servo to 0
        }
        void start_cycle() {
            // set servo to 1
        }
        void stop_cycle() {
            // set servo to 0
        }
        bool is_tripped() { return _probePin.read(); }

        ~BLTouch() {}
    };

}
