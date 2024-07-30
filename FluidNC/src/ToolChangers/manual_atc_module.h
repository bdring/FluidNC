#pragma once

#include "src/Config.h"

#include "src/Configuration/Configurable.h"

#include "src/Channel.h"
#include "src/Module.h"

typedef const uint8_t* font_t;

class Manual_ATC : public Module {
public:
    Manual_ATC(const char* name) : Module(name) {}

private:
    // config items
    float              _safe_z           = 50.0;
    float              _probe_seek_rate  = 200.0;
    float              _probe_feed_rate  = 80.0;
    std::vector<float> _ets_mpos         = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    std::vector<float> _change_mpos      = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };  // manual tool change location
    float              _ets_rapid_z_mpos = 0;
    bool               _error            = false;

public:
    Manual_ATC() : Module("atc_manual") {}

    Manual_ATC(const Manual_ATC&)            = delete;
    Manual_ATC(Manual_ATC&&)                 = delete;
    Manual_ATC& operator=(const Manual_ATC&) = delete;
    Manual_ATC& operator=(Manual_ATC&&)      = delete;

    virtual ~Manual_ATC() = default;

    void init() override;

    Manual_ATC* __atc;

    // Configurable

    // Channel method overrides

    // Configuration handlers:
    void validate() override {}

    void afterParse() override;

    void group(Configuration::HandlerBase& handler) override {
        handler.item("safe_z_mpos_mm", _safe_z, -100000, 100000);
        handler.item("probe_seek_rate_mm_per_min", _probe_seek_rate, 1, 10000);
        handler.item("probe_feed_rate_mm_per_min", _probe_feed_rate, 1, 10000);
        handler.item("change_mpos_mm", _change_mpos);
        handler.item("ets_mpos_mm", _ets_mpos);
        handler.item("ets_rapid_z_mpos_mm", _ets_rapid_z_mpos);
    }
};
