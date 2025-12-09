// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    A Base class for spindles and spindle like things such as lasers
*/
#include "Spindle.h"

#include "System.h"  //sys.spindle_speed_ovr

Spindles::Spindle* spindle = nullptr;

namespace Spindles {
    // ========================= Spindle ==================================

    void Spindle::init_atc() {
        auto atcs = ATCs::ATCFactory::objects();
        _atc_name = string_util::trim(_atc_name);
        for (auto a : atcs) {
            if (_atc_name == a->name()) {
                _atc      = a;
                _atc_info = " atc:" + _atc_name;
                return;
            }
        }
        if (!_atc_name.empty()) {
            _atc_info = " atc: '" + _atc_name + "' not found";
        } else if (!_m6_macro._gcode.empty()) {
            _atc_info = " with m6_macro";
        }
    }

    void Spindle::switchSpindle(uint32_t new_tool, SpindleList spindles, Spindle*& spindle, bool& stop_spindle, bool& new_spindle) {
        // Find the spindle whose tool number is closest to and below the new tool number
        Spindle* candidate = nullptr;
        for (auto s : spindles) {
            if (s->_tool <= new_tool && (!candidate || candidate->_tool < s->_tool)) {
                candidate = s;
            }
        }
        if (candidate) {
            if (spindle != nullptr) {
                spindle->stop();      // stop the current spindle
                stop_spindle = true;  // used to stop the next spindle
            }
            if (candidate != spindle) {  // we are changing spindles
                gc_state.selected_tool = new_tool;
                spindle                = candidate;
                new_spindle            = true;
                log_info("Changed to spindle:" << spindle->name());
            }
        } else {
            if (spindle == nullptr) {
                if (spindles.size() == 0) {
                    log_error("No spindles are defined");
                    return;
                }
                spindle = spindles[0];
            }
        }
    }

    bool Spindle::isRateAdjusted() {
        return false;  // default for basic spindle is false
    }

    void Spindle::setupSpeeds(uint32_t max_dev_speed) {
        size_t nsegments = _speeds.size() - 1;
        if (nsegments < 1) {
            return;
        }
        size_t i;

        SpindleSpeed offset;
        uint32_t     scaler;

        // For additional segments we compute a scaler that is the slope
        // of the segment and an offset that is the starting Y (typically
        // PWM value) for the segment.
        for (i = 0; i < nsegments; i++) {
            offset            = _speeds[i].percent / 100.0 * max_dev_speed;
            _speeds[i].offset = offset;

            float deltaPercent = (_speeds[i + 1].percent - _speeds[i].percent) / 100.0f;
            float deltaRPM     = _speeds[i + 1].speed - _speeds[i].speed;
            float scale        = deltaRPM == 0.0f ? 0.0f : (deltaPercent / deltaRPM);
            scale *= max_dev_speed;

            // float scale = deltaPercent * max_dev_speed;
            scaler           = uint32_t(scale * 65536);  //  computation is done in fixed point with 16 fractional bits.
            _speeds[i].scale = scaler;
        }

        // The final scaler is 0, with the offset equal to the ending offset
        offset            = SpindleSpeed(_speeds[nsegments].percent / 100.0f * float(max_dev_speed));
        _speeds[i].offset = offset;
        scaler            = 0;
        _speeds[i].scale  = scaler;
    }

    void Spindle::validate() {
        for (auto s : Spindles::SpindleFactory::objects()) {
            Assert(s == this || s->_tool != _tool, "Duplicate tool_number %d with /%s", _tool, s->name());
        }
    }

    void Spindle::afterParse() {
        if (_speeds.size() && !maxSpeed()) {
            log_error("Speed map max speed is 0. Using default");
            _speeds.clear();
        }
    }

    void Spindle::linearSpeeds(SpindleSpeed maxSpeed, float maxPercent) {
        _speeds.clear();
        _speeds.push_back({ 0, 0.0f });
        _speeds.push_back({ maxSpeed, maxPercent });
    }

    void Spindle::shelfSpeeds(SpindleSpeed min, SpindleSpeed max) {
        float minPercent = 100.0f * min / max;
        _speeds.clear();
        _speeds.push_back({ 0, 0.0f });
        _speeds.push_back({ 0, minPercent });
        if (min) {
            _speeds.push_back({ min, minPercent });
        }
        _speeds.push_back({ max, 100.0f });
    }

    // pre_select is generally ignored except for machines that need to get a tool ready
    // set_tool is just used to tell the atc what is already installed.
    bool Spindle::tool_change(uint32_t tool_number, bool pre_select, bool set_tool) {
        if (_atc != NULL) {
            log_info(_name << " spindle changed to tool:" << tool_number << " using " << _atc_name);
            return _atc->tool_change(tool_number, pre_select, set_tool);
        }
        if (!_m6_macro.get().empty()) {
            if (pre_select) {
                return true;
            }
            _last_tool = tool_number;
            if (set_tool) {
                return true;
            }
            _m6_macro.run(nullptr);
            _last_tool = tool_number;
            return true;
            //}
        }

        return true;
    }

    uint32_t Spindle::maxSpeed() {
        if (_speeds.size() == 0) {
            return 0;
        } else {
            return _speeds[_speeds.size() - 1].speed;
        }
    }

    uint32_t IRAM_ATTR Spindle::mapSpeed(SpindleState state, SpindleSpeed speed) {
        speed = speed * sys.spindle_speed_ovr() / 100;
        sys.set_spindle_speed(speed);
        if (state == SpindleState::Disable) {  // Halt or set spindle direction and speed.
            if (_zero_speed_with_disable) {
                sys.set_spindle_speed(0);
                return 0;
            }
        }
        if (_speeds.size() == 0) {
            return 0;
        }
        if (speed < _speeds[0].speed) {
            return _speeds[0].offset;
        }
        if (speed == 0) {
            return _speeds[0].offset;
        }
        size_t num_segments = _speeds.size() - 1;
        size_t i;
        for (i = 0; i < num_segments; i++) {
            if (speed < _speeds[i + 1].speed) {
                break;
            }
        }

        // if the offset is that max value of uint32, then the offset was never set. therefore, bypass the mapping process
        if(_speeds[i].offset == -1) {
            return speed;
        }

        uint32_t dev_speed = _speeds[i].offset;

        // If the requested speed is greater than the maximum map speed,
        // i will be equal to num_segements, in which case we just return
        // the maximum dev_speed.

        // Otherwise, we interpolate by applying the segment scale factor
        // to the segment offset .
        if (i < num_segments) {
            dev_speed += uint32_t((((speed - _speeds[i].speed) * uint64_t(_speeds[i].scale)) >> 16));
        }

        // log_debug("rpm " << speed << " speed " << dev_speed); // This will spew quite a bit of data on your output
        return dev_speed;
    }
    void Spindle::spindleDelay(SpindleState state, SpindleSpeed speed) {
        uint32_t up = 0, down = 0;
        switch (state) {
            case SpindleState::Unknown:
                // Unknown is only used for an initializer value,
                // never as a new target state.
                break;
            case SpindleState::Disable:
                switch (_current_state) {
                    case SpindleState::Unknown:
                        down = maxSpeed();
                        break;
                    case SpindleState::Disable:
                        break;
                    case SpindleState::Cw:
                    case SpindleState::Ccw:
                        down = _current_speed;
                        break;
                }
                break;
            case SpindleState::Cw:
                switch (_current_state) {
                    case SpindleState::Unknown:
                        down = maxSpeed();
                        // fall through
                    case SpindleState::Disable:
                        up = speed;
                        break;
                    case SpindleState::Cw:
                        if (speed > _current_speed) {
                            up = speed - _current_speed;
                        } else {
                            down = speed - _current_speed;
                        }
                        break;
                    case SpindleState::Ccw:
                        down = _current_speed;
                        up   = speed;
                        break;
                }
                break;
            case SpindleState::Ccw:
                switch (_current_state) {
                    case SpindleState::Unknown:
                        down = maxSpeed();
                        // fall through
                    case SpindleState::Disable:
                        up = speed;
                        break;
                    case SpindleState::Cw:
                        down = _current_speed;
                        up   = speed;
                        break;
                    case SpindleState::Ccw:
                        if (speed > _current_speed) {
                            up = speed - _current_speed;
                        } else {
                            down = speed - _current_speed;
                        }
                        break;
                }
                break;
        }
        if (down) {
            dwell_ms(down < maxSpeed() ? _spindown_ms * down / maxSpeed() : _spindown_ms, DwellMode::SysSuspend);
        }
        if (up) {
            dwell_ms(up < maxSpeed() ? _spinup_ms * up / maxSpeed() : _spinup_ms, DwellMode::SysSuspend);
        }
        _current_state = state;
        _current_speed = speed;
    }
}
