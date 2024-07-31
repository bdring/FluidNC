#pragma once

#include "src/Config.h"

#include "src/Configuration/Configurable.h"

#include "src/Channel.h"
#include "src/Module.h"
#include "atc.h"

namespace ATCs {
    class Manual_ATC : public ATC {
    public:
        Manual_ATC(const char* name) : ATC(name) {}

        Manual_ATC(const Manual_ATC&)  = delete;
        Manual_ATC(Manual_ATC&&)       = delete;
        Manual_ATC& operator=(const Manual_ATC&) = delete;
        Manual_ATC& operator=(Manual_ATC&&)      = delete;

        virtual ~Manual_ATC() = default;

    private:
        // config items
        float              _safe_z           = 50.0;
        float              _probe_seek_rate  = 200.0;
        float              _probe_feed_rate  = 80.0;
        std::vector<float> _ets_mpos         = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
        std::vector<float> _change_mpos      = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };  // manual tool change location
        float              _ets_rapid_z_mpos = 0;

    public:
        

        void init() override;
        void probe_notification() override;
        void tool_change(uint8_t value, bool pre_select) override;

        void validate() override {}


        void group(Configuration::HandlerBase& handler) override {
            handler.item("safe_z_mpos_mm", _safe_z, -100000, 100000);
            handler.item("probe_seek_rate_mm_per_min", _probe_seek_rate, 1, 10000);
            handler.item("probe_feed_rate_mm_per_min", _probe_feed_rate, 1, 10000);
            handler.item("change_mpos_mm", _change_mpos);
            handler.item("ets_mpos_mm", _ets_mpos);
            handler.item("ets_rapid_z_mpos_mm", _ets_rapid_z_mpos);
        }
    };
}
