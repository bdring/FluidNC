/*
    Spindle.cpp

    A Base class for spindles and spinsle like things such as lasers

    Part of Grbl_ESP32

    2020 -  Bart Dring

    Grbl is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    Grbl is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with Grbl.  If not, see <http://www.gnu.org/licenses/>.

    TODO
        Add Spindle spin up/down delays

*/
#include "Spindle.h"

#include "../System.h"  //sys.spindle_speed_ovr
#include <esp32-hal.h>  // delay()

Spindles::Spindle* spindle = nullptr;

namespace Spindles {
    // ========================= Spindle ==================================

    void Spindle::switchSpindle(uint8_t new_tool, SpindleList spindles, Spindle*& spindle) {
        // Find the spindle whose tool number is closest to and below the new tool number
        Spindle* candidate = nullptr;
        for (auto s : spindles) {
            if (s->_tool <= new_tool && (!candidate || candidate->_tool < s->_tool)) {
                candidate = s;
            }
        }
        if (candidate) {
            if (candidate != spindle) {
                if (spindle != nullptr) {
                    spindle->stop();
                }
                spindle = candidate;
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
        log_info("Using spindle " << spindle->name());
    }

    bool Spindle::isRateAdjusted() {
        return false;  // default for basic spindle is false
    }

    void Spindle::setupSpeeds(uint32_t max_dev_speed) {
        int nsegments = _speeds.size() - 1;
        if (nsegments < 1) {
            return;
        }
        int i;

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
            scaler           = uint32_t(scale * 65536);
            _speeds[i].scale = scaler;
        }

        // The final scaler is 0, with the offset equal to the ending offset
        offset            = SpindleSpeed(_speeds[nsegments].percent / 100.0f * float(max_dev_speed));
        _speeds[i].offset = offset;
        scaler            = 0;
        _speeds[i].scale  = scaler;
    }

    void Spindle::afterParse() {}

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

    uint32_t Spindle::mapSpeed(SpindleSpeed speed) {
        speed             = speed * sys.spindle_speed_ovr / 100;
        sys.spindle_speed = speed;
        if (speed < _speeds[0].speed) {
            return _speeds[0].offset;
        }
        if (speed == 0) {
            return _speeds[0].offset;
        }
        int num_segments = _speeds.size() - 1;
        int i;
        for (i = 0; i < num_segments; i++) {
            if (speed < _speeds[i + 1].speed) {
                break;
            }
        }
        uint32_t dev_speed = _speeds[i].offset;

        // If the requested speed is greater than the maximum map speed,
        // i will be equal to num_segements, in which case we just return
        // the maximum dev_speed.

        // Otherwise, we interpolate by applying the segment scale factor
        // to the segment offset .
        if (i < num_segments) {
            dev_speed += (((speed - _speeds[i].speed) * _speeds[i].scale) >> 16);
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
        }
        if (down) {
            delay(_spindown_ms * down / maxSpeed());
        }
        if (up) {
            delay(_spinup_ms * up / maxSpeed());
        }
        _current_state = state;
        _current_speed = speed;
    }
}
