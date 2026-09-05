// Copyright (c) 2024 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "VFDProtocol.h"
#include "Spindles/VFDSpindle.h"
#include <string_view>

namespace Spindles {
    namespace VFD {
        class ModbusVFD : public VFDProtocol, Configuration::Configurable {
        private:
            void scale(uint32_t& n, std::string_view scale_str, uint32_t maxRPM);
            bool set_data(std::string_view                 token,
                          std::basic_string_view<uint8_t>& response_view,
                          const char*                      name,
                          uint32_t&                        data,
                          bool                             is_big_endian);

        protected:
            void direction_command(SpindleState mode, ModbusCommand& data) override;
            void set_speed_command(uint32_t dev_speed, ModbusCommand& data) override;

            response_parser initialization_sequence(int index, ModbusCommand& data, VFDSpindle* vfd) override;
            response_parser get_current_speed(ModbusCommand& data) override;
            response_parser get_current_direction(ModbusCommand& data) override { return nullptr; };
            response_parser get_status_ok(ModbusCommand& data) override { return nullptr; }

            bool _safetyPolling = false;

            std::string _cw_cmd;
            std::string _ccw_cmd;
            std::string _off_cmd;
            std::string _set_rpm_cmd;
            std::string _get_min_rpm_cmd;
            std::string _get_max_rpm_cmd;
            std::string _get_rpm_cmd;

            bool use_speed_feedback() const override { return !_get_rpm_cmd.empty(); }
            bool safety_polling() const override { return _safetyPolling; }

        private:
            std::string _model;  // VFD Model name
            uint32_t*   _response_data;
            uint32_t    _minRPM = 0xffffffff;
            uint32_t    _maxRPM = 0xffffffff;

            VFDSpindle* spindle;

            bool        parser(const uint8_t* response, VFDSpindle* spindle, ModbusVFD* protocol);
            void        send_vfd_command(const std::string cmd, ModbusCommand& data, uint32_t out);
            std::string _response_format;

        public:
            ModbusVFD() {}
            ModbusVFD(const char* model) : _model(model) {}
            ModbusVFD(const char* model,
                      uint32_t    min_rpm,
                      uint32_t    max_rpm,
                      const char* cw_cmd,
                      const char* ccw_cmd,
                      const char* off_cmd,
                      const char* set_rpm_cmd,
                      const char* get_rpm_cmd,
                      const char* get_min_rpm_cmd,
                      const char* get_max_rpm_cmd) :
                _model(model), _minRPM(min_rpm), _maxRPM(max_rpm), _cw_cmd(cw_cmd), _ccw_cmd(ccw_cmd), _off_cmd(off_cmd),
                _set_rpm_cmd(set_rpm_cmd), _get_min_rpm_cmd(get_min_rpm_cmd), _get_max_rpm_cmd(get_max_rpm_cmd), _get_rpm_cmd(get_rpm_cmd) {}

            void group(Configuration::HandlerBase& handler) override {
                // @config safety_polling
                // @default false
                // @default_note ""
                // VFD is is polled for speed continously
                handler.item("safety_polling", _safetyPolling);

                // Generic/raw ModbusVFD spindle -- use this directly only for an
                // unsupported VFD model; a supported model (e.g. Huanyang) instead
                // registers under its own model-specific config name, but shows every one
                // of these same fields with model-appropriate values.

                // @config model
                // @default ""
                // @default_note empty
                // @tuning per-machine
                // VFD model name. Informational (used for support purposes) -- not itself
                // used to select protocol behavior; the cw_cmd/ccw_cmd/etc. fields below
                // are what actually define the protocol. Only meaningful under the plain
                // ModbusVFD: top-level key, for a custom/unsupported VFD -- a supported
                // model (e.g. H2A, Huanyang) is instead selected by using that model's own
                // top-level config key, which supplies all of the fields below itself; this
                // field is never read at runtime, so setting it under ModbusVFD: does NOT
                // apply that model's own values.
                handler.item("model", _model);

                // @config min_RPM
                // @default 0xffffffff
                // @default_note uninitialized sentinel
                // @tuning per-machine
                // Minimum spindle RPM, in Hz-derived RPM terms. Normally left unset and
                // retrieved from the VFD itself via get_min_rpm_cmd at startup.
                handler.item("min_RPM", _minRPM);

                // @config max_RPM
                // @default 0xffffffff
                // @default_note uninitialized sentinel
                // @tuning per-machine
                // Maximum spindle RPM. Normally left unset and retrieved from the VFD
                // itself via get_max_rpm_cmd at startup.
                handler.item("max_RPM", _maxRPM);

                // @config cw_cmd
                // @default ""
                // @default_note empty
                // @tuning per-machine
                // Modbus command template for clockwise spindle rotation.
                handler.item("cw_cmd", _cw_cmd);

                // @config ccw_cmd
                // @default ""
                // @default_note empty
                // @tuning per-machine
                // Modbus command template for counter-clockwise spindle rotation.
                handler.item("ccw_cmd", _ccw_cmd);

                // @config off_cmd
                // @default ""
                // @default_note empty
                // @tuning per-machine
                // Modbus command template to stop the spindle.
                handler.item("off_cmd", _off_cmd);

                // @config set_rpm_cmd
                // @default ""
                // @default_note empty
                // @tuning per-machine
                // Modbus command template to set the spindle speed.
                handler.item("set_rpm_cmd", _set_rpm_cmd);

                // @config get_min_rpm_cmd
                // @default ""
                // @default_note empty
                // @tuning per-machine
                // Modbus command template to query the VFD's minimum RPM. If left unset,
                // speed_map must be configured manually instead of being auto-derived.
                handler.item("get_min_rpm_cmd", _get_min_rpm_cmd);

                // @config get_max_rpm_cmd
                // @default ""
                // @default_note empty
                // @tuning per-machine
                // Modbus command template to query the VFD's maximum RPM. If left unset,
                // speed_map must be configured manually instead of being auto-derived.
                handler.item("get_max_rpm_cmd", _get_max_rpm_cmd);

                // @config get_rpm_cmd
                // @default ""
                // @default_note empty
                // @tuning per-machine
                // Modbus command template to query the VFD's current RPM. spinup_ms/
                // spindown_ms (Spindle::groupDelaySettings()) are always present in
                // config.yaml for a VFD spindle regardless of this field -- what this
                // field changes is whether they're actually used at runtime. When set,
                // use_speed_feedback() returns true and VFDSpindle actively polls this
                // command until it confirms the real target speed, ignoring spinup_ms/
                // spindown_ms entirely; when left empty, there's no way to confirm real
                // speed, so it falls back to blindly waiting out spinup_ms/spindown_ms
                // instead.
                handler.item("get_rpm_cmd", _get_rpm_cmd);
            }
        };
    }
}
