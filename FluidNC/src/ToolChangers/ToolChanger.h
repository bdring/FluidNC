#pragma once

#include "../Config.h"
#include "../Configuration/Configurable.h"

namespace ToolChangers {
    class ToolChanger : public Configuration::Configurable {
    private:
        uint8_t            _prev_tool       = 0;  // TODO This could be a NV setting
        float              _safe_z          = 50.0;
        float              _probe_seek_rate = 200.0;
        float              _probe_feed_rate = 80.0;
        std::vector<float> _ets_mpos = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        std::vector<float> _change_mpos = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 }; // manual tool change location
        bool               _is_OK                   = false;
        bool               _have_tool_setter_offset = false;
        float              _tool_setter_offset      = 0.0;  // have we established an offset.
        uint8_t            _zeroed_tool_index       = 0;
        float              _tool_setter_position[MAX_N_AXIS];

        void reset();  // reset values to default
        void move_to_change_location();
        void move_to_save_z();
        void move_over_toolsetter();
        bool probe(float rate, float* probe_z_mpos);
        bool seek_probe();

    public:
        ToolChanger() = default;

        ToolChanger(const ToolChanger&)            = delete;
        ToolChanger(ToolChanger&&)                 = delete;
        ToolChanger& operator=(const ToolChanger&) = delete;
        ToolChanger& operator=(ToolChanger&&)      = delete;

        virtual ~ToolChanger() = default;

        virtual void init();
        bool         tool_change(uint8_t new_tool, bool pre_select);
        void         probe_notification() {};  //
        void         deactivate() {};          // used when changing spindles (not tools)
        bool         is_OK();                  //
        bool         hold_and_wait_for_resume();

        void group(Configuration::HandlerBase& handler) override {
            handler.item("safe_z_mpos_mm", _safe_z, -100000, 100000);
            handler.item("probe_seek_rate_mm_per_min", _probe_seek_rate, 1, 10000);
            handler.item("probe_feed_rate_mm_per_min", _probe_feed_rate, 1, 10000);
            handler.item("change_mpos_mm", _change_mpos);
            handler.item("ets_mpos_mm", _ets_mpos);
        }

        using ChangerFactory = Configuration::GenericFactory<ToolChanger>;
    };
}