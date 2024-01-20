// Copyright (c) 2023 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is a class for status "Idle,Run,Hold,Alarm" pins.

    This can be used for Tower lights,etc.
*/
#include "Status_outputs.h"
#include "Machine/MachineConfig.h"

void Status_Outputs::init() {
    if (_Idle_pin.defined()) {
        _Idle_pin.setAttr(Pin::Attr::Output);
    }

    if (_Run_pin.defined()) {
        _Run_pin.setAttr(Pin::Attr::Output);
    }

    if (_Hold_pin.defined()) {
        _Hold_pin.setAttr(Pin::Attr::Output);
    }

    if (_Alarm_pin.defined()) {
        _Alarm_pin.setAttr(Pin::Attr::Output);
    }

    log_info("Status outputs"
             << " Interval:" << _report_interval_ms << " Idle:" << _Idle_pin.name() << " Cycle:" << _Run_pin.name()
             << " Hold:" << _Hold_pin.name() << " Alarm:" << _Alarm_pin.name());

    allChannels.registration(this);
    setReportInterval(_report_interval_ms);
}

void Status_Outputs::parse_report() {
    if (_report.rfind("<", 0) == 0) {
        parse_status_report();
        return;
    }
}

// This is how the OLED driver receives channel data
size_t Status_Outputs::write(uint8_t data) {
    char c = data;
    if (c == '\r') {
        return 1;
    }
    if (c == '\n') {
        parse_report();
        _report = "";
        return 1;
    }
    _report += c;
    return 1;
}

Channel* Status_Outputs::pollLine(char* line) {
    autoReport();
    return nullptr;
}

void Status_Outputs::parse_status_report() {
    if (_report.back() == '>') {
        _report.pop_back();
    }
    // Now the string is a sequence of field|field|field
    size_t pos     = 0;
    auto   nextpos = _report.find_first_of("|", pos);
    _state         = _report.substr(pos + 1, nextpos - pos - 1);

    _Idle_pin.write(_state == "Idle");
    _Run_pin.write(_state == "Run");
    _Hold_pin.write(_state.substr(0, 4) == "Hold");
    _Alarm_pin.write(_state == "Alarm");
}
