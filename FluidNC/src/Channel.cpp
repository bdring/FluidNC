// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Channel.h"
#include "Report.h"                 // report_gcode_modes
#include "Machine/MachineConfig.h"  // config
#include "RealtimeCmd.h"            // execute_realtime_command
#include "Limits.h"

void Channel::flushRx() {
    _linelen   = 0;
    _lastWasCR = false;
    while (_queue.size()) {
        _queue.pop();
    }
}

bool Channel::lineComplete(char* line, char ch) {
    // The objective here is to treat any of CR, LF, or CR-LF
    // as a single line ending.  When we see CR, we immediately
    // complete the line, setting a flag to say that the last
    // character was CR.  When we see LF, if the last character
    // was CR, we ignore the LF because the line has already
    // been completed, otherwise we complete the line.
    if (ch == '\n') {
        if (_lastWasCR) {
            _lastWasCR = false;
            return false;
        }
        // if (_discarding) {
        //     _linelen = 0;
        //     _discarding = false;
        //     return nullptr;
        // }

        // Return the complete line
        _line[_linelen] = '\0';
        strcpy(line, _line);
        _linelen = 0;
        return true;
    }
    _lastWasCR = ch == '\r';
    if (_lastWasCR) {
        // Return the complete line
        _line[_linelen] = '\0';
        strcpy(line, _line);
        _linelen = 0;
        return true;
    }
    if (ch == '\b') {
        // Simple editing for interactive input - backspace erases
        if (_linelen) {
            --_linelen;
        }
        return false;
    }
    if (_linelen < (Channel::maxLine - 1)) {
        _line[_linelen++] = ch;
    } else {
        //  report_status_message(Error::Overflow, this);
        // _linelen = 0;
        // Probably should discard the rest of the line too.
        // _discarding = true;
    }
    return false;
}

uint32_t Channel::setReportInterval(uint32_t ms) {
    uint32_t actual = ms;
    if (actual) {
        actual = std::max(actual, uint32_t(50));
    }
    _reportInterval = actual;
    _nextReportTime = int32_t(xTaskGetTickCount());
    _lastTool       = 255;  // Force GCodeState report
    return actual;
}
static bool motionState() {
    return sys.state == State::Cycle || sys.state == State::Homing || sys.state == State::Jog;
}

void Channel::autoReportGCodeState() {
    // When moving, we suppress $G reports in which the only change is the motion mode
    // (e.g. G0/G1/G2/G3 changes) because rapid-fire motion mode changes are fairly common.
    // We would rather not issue a $G report after every GCode line.
    // Similarly, F and S values can change rapidly, especially in laser programs.
    // F and S values are also reported in ? status reports, so they will show up
    // at the chosen periodic rate there.
    if (motionState()) {
        // Force the compare to succeed if the only change is the motion mode
        _lastModal.motion = gc_state.modal.motion;
    }
    if (memcmp(&_lastModal, &gc_state.modal, sizeof(_lastModal)) || _lastTool != gc_state.tool ||
        (!motionState() && (_lastSpindleSpeed != gc_state.spindle_speed || _lastFeedRate != gc_state.feed_rate))) {
        report_gcode_modes(*this);
        memcpy(&_lastModal, &gc_state.modal, sizeof(_lastModal));
        _lastTool         = gc_state.tool;
        _lastSpindleSpeed = gc_state.spindle_speed;
        _lastFeedRate     = gc_state.feed_rate;
    }
}
void Channel::autoReport() {
    if (_reportInterval) {
        auto limitState = limits_get_state();
        auto probeState = config->_probe->get_state();
        if (_reportWco || sys.state != _lastState || limitState != _lastLimits || probeState != _lastProbe ||
            (motionState() && (int32_t(xTaskGetTickCount()) - _nextReportTime) >= 0)) {
            if (_reportWco) {
                report_wco_counter = 0;
            }
            _reportWco  = false;
            _lastState  = sys.state;
            _lastLimits = limitState;
            _lastProbe  = probeState;

            _nextReportTime = xTaskGetTickCount() + _reportInterval;
            report_realtime_status(*this);
        }
        if (_reportNgc != CoordIndex::End) {
            report_ngc_coord(_reportNgc, *this);
            _reportNgc = CoordIndex::End;
        }
        autoReportGCodeState();
    }
}

Channel* Channel::pollLine(char* line) {
    handle();
    while (1) {
        int ch;
        if (line && _queue.size()) {
            ch = _queue.front();
            _queue.pop();
        } else {
            ch = read();
        }

        // ch will only be negative if read() was called and returned -1
        // The _queue path will return only nonnegative character values
        if (ch < 0) {
            break;
        }
        if (_last_rt_cmd == Cmd::PinLow) {
            try {
                auto event_pin  = _events.at(ch);
                _pin_values[ch] = false;
                event_pin->trigger(false);
            } catch (std::exception& ex) {}
            _last_rt_cmd = Cmd::None;
            continue;
        }
        if (_last_rt_cmd == Cmd::PinHigh) {
            try {
                auto event_pin  = _events.at(ch);
                _pin_values[ch] = true;
                event_pin->trigger(true);
            } catch (std::exception& ex) {}
            _last_rt_cmd = Cmd::None;
            continue;
        }
        if (realtimeOkay(ch)) {
            if (is_extended_realtime_command(ch)) {
                _last_rt_cmd = static_cast<Cmd>(ch);
                continue;
            }
            if (is_realtime_command(ch)) {
                execute_realtime_command(static_cast<Cmd>(ch), *this);
                continue;
            }
        }
        if (!line) {
            // If we are not able to handle a line we save the character
            // until later
            _queue.push(uint8_t(ch));
            continue;
        }
        if (line && lineComplete(line, ch)) {
            return this;
        }
    }
    autoReport();
    return nullptr;
}

void Channel::setAttr(int index, Pins::PinAttributes attr) {
    // XXX send INI message
    _pin_attributes[index] = attr;
}
Pins::PinAttributes Channel::getAttr(int index) const {
    try {
        return _pin_attributes.at(index);
    } catch (std::exception& ex) { return Pins::PinAttributes::None; }
}

void Channel::out(int index, int value) {
    _pin_values[index] = value;
    // XXX send SET message
}
int Channel::in(int index) {
    return _pin_values[index];
}

void Channel::registerEvent(uint8_t code, EventPin* obj) {
    _events[code] = obj;
}

void Channel::ack(Error status) {
    if (status == Error::Ok) {
        log_to(*this, "ok");
        return;
    }
    // With verbose errors, the message text is displayed instead of the number.
    // Grbl 0.9 used to display the text, while Grbl 1.1 switched to the number.
    // Many senders support both formats.
    LogStream msg(*this, "error:");
    if (config->_verboseErrors) {
        msg << errorString(status);
    } else {
        msg << static_cast<int>(status);
    }
}
