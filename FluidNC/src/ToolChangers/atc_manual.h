// Copyright (c) 2024 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Config.h"

#include "Configuration/Configurable.h"

#include "Channel.h"
#include "Module.h"
#include "atc.h"
#include "Machine/Macros.h"

namespace ATCs {
    class Manual_ATC : public ATC {
    public:
        Manual_ATC(const char* name) : ATC(name) {}

        Manual_ATC(const Manual_ATC&)            = delete;
        Manual_ATC(Manual_ATC&&)                 = delete;
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

        bool   _is_OK                   = false;
        tool_t _prev_tool               = 0;  // TODO This could be a NV setting
        bool   _have_tool_setter_offset = false;
        float  _tool_setter_offset      = 0.0;  // have we established an offset.
        float  _tool_setter_position[MAX_N_AXIS];

        void move_to_change_location();
        void move_to_safe_z();
        void move_over_toolsetter();
        void ets_probe();
        void reset();

        Macro _macro;

    public:
        void init() override;
        void probe_notification() override;
        bool tool_change(tool_t value, bool pre_select, bool set_tool) override;

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
