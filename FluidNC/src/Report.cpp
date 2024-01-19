// Copyright (c) 2012-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  Report.cpp - reporting and messaging methods
*/

/*
  This file functions as the primary feedback interface. Any outgoing data, such
  as the protocol status messages, feedback messages, and status reports, are stored here.
  For the most part, these functions primarily are called from Protocol.cpp methods. If a
  different style feedback is desired (i.e. JSON), then a user can change these following
  methods to accommodate their needs.


  ESP32 Notes:

*/

#include "Report.h"

#include "Machine/MachineConfig.h"
#include "SettingsDefinitions.h"
#include "MotionControl.h"               // probe_succeeded
#include "Limits.h"                      // limits_get_state
#include "Planner.h"                     // plan_get_block_buffer_available
#include "Stepper.h"                     // step_count
#include "Platform.h"                    // WEAK_LINK
#include "WebUI/NotificationsService.h"  // WebUI::notificationsService
#include "WebUI/WifiConfig.h"            // wifi_config
#include "WebUI/BTConfig.h"              // bt_config
#include "WebUI/WebSettings.h"
#include "InputFile.h"

#include <map>
#include <freertos/task.h>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <sstream>
#include <iomanip>

#ifdef DEBUG_REPORT_HEAP
EspClass esp;
#endif

volatile bool protocol_pin_changed = false;

std::string report_pin_string;

portMUX_TYPE mmux = portMUX_INITIALIZER_UNLOCKED;

void _notify(const char* title, const char* msg) {
    WebUI::notificationsService.sendMSG(title, msg);
}

void _notifyf(const char* title, const char* format, ...) {
    char    loc_buf[64];
    char*   temp = loc_buf;
    va_list arg;
    va_list copy;
    va_start(arg, format);
    va_copy(copy, arg);
    size_t len = vsnprintf(NULL, 0, format, arg);
    va_end(copy);
    if (len >= sizeof(loc_buf)) {
        temp = new char[len + 1];
        if (temp == NULL) {
            return;
        }
    }
    len = vsnprintf(temp, len + 1, format, arg);
    _notify(title, temp);
    va_end(arg);
    if (temp != loc_buf) {
        delete[] temp;
    }
}

Counter report_ovr_counter = 0;
Counter report_wco_counter = 0;

static const int coordStringLen = 20;
static const int axesStringLen  = coordStringLen * MAX_N_AXIS;

// Sends the axis values to the output channel
static std::string report_util_axis_values(const float* axis_value) {
    std::ostringstream msg;
    auto               n_axis = config->_axes->_numberAxis;
    for (size_t idx = 0; idx < n_axis; idx++) {
        int   decimals;
        float value = axis_value[idx];
        if (idx >= A_AXIS && idx <= C_AXIS) {
            // Rotary axes are in degrees so mm vs inch is not
            // relevant.  Three decimal places is probably overkill
            // for rotary axes but we use 3 in case somebody wants
            // to use ABC as linear axes in mm.
            decimals = 3;
        } else {
            if (config->_reportInches) {
                value /= MM_PER_INCH;
                decimals = 4;  // Report inches to 4 decimal places
            } else {
                decimals = 3;  // Report mm to 3 decimal places
            }
        }
        msg << std::fixed << std::setprecision(decimals) << value;
        if (idx < (n_axis - 1)) {
            msg << ",";
        }
    }
    return msg.str();
}

std::map<Message, const char*> MessageText = {
    { Message::CriticalEvent, "Reset to continue" },
    { Message::AlarmLock, "'$H'|'$X' to unlock" },
    { Message::AlarmUnlock, "Caution: Unlocked" },
    { Message::Enabled, "Enabled" },
    { Message::Disabled, "Disabled" },
    { Message::SafetyDoorAjar, "Check door" },
    { Message::CheckLimits, "Check limits" },
    { Message::ProgramEnd, "Program End" },
    { Message::RestoreDefaults, "Restoring defaults" },
    { Message::SpindleRestore, "Restoring spindle" },
    { Message::SleepMode, "Sleeping" },
    { Message::HardStop, "Hard stop" },
    { Message::ConfigAlarmLock, "Configuration is invalid. Check boot messages for ERR's." },
    // Handled separately due to numeric argument
    // { Message::FileQuit, "Reset during file job at line: %d" },
};

// Prints feedback messages. This serves as a centralized method to provide additional
// user feedback for things that are not of the status/alarm message protocol. These are
// messages such as setup warnings, switch toggling, and how to exit alarms.
// NOTE: For interfaces, messages are always placed within brackets. And if silent mode
// is installed, the message number codes are less than zero.
void report_feedback_message(Message message) {  // ok to send to all channels
    auto it = MessageText.find(message);
    if (it != MessageText.end()) {
        log_info(it->second);
    }
}
void report_error_message(Message message) {  // ok to send to all channels
    auto it = MessageText.find(message);
    if (it != MessageText.end()) {
        log_error(it->second);
    }
}

const char* radio =
#if defined(ENABLE_WIFI) || defined(ENABLE_BLUETOOTH)
#    if defined(ENABLE_WIFI) && defined(ENABLE_BLUETOOTH)
    "wifi+bt";
#    else
#        ifdef ENABLE_WIFI
    "wifi";
#        endif
#        ifdef ENABLE_BLUETOOTH
"bt";
#        endif
#    endif
#else
    "noradio";
#endif

// Welcome message
void report_init_message(Channel& channel) {
    log_string(channel, "");  // Empty line for spacer
    LogStream   msg(channel, "");
    const char* p = start_message->get();
    char        c;
    while ((c = *p++) != '\0') {
        if (c == '\\') {
            switch ((c = *p++)) {
                case '\0':
                    --p;  // Unconsume the null character
                    break;
                case 'H':
                    msg << "'$' for help";
                    break;
                case 'B':
                    msg << git_info;
                    break;
                case 'V':
                    msg << grbl_version;
                    break;
                case 'R':
                    msg << radio;
                    break;
                default:
                    msg << c;
                    break;
            }
        } else {
            msg << c;
        }
    }
    // When msg goes out of scope, the destructor will send the line
}

// Prints current probe parameters. Upon a probe command, these parameters are updated upon a
// successful probe or upon a failed probe with the G38.3 without errors command (if supported).
// These values are retained until the system is power-cycled, whereby they will be re-zeroed.
void report_probe_parameters(Channel& channel) {
    // Report in terms of machine position.
    // get the machine position and put them into a string and append to the probe report
    float print_position[MAX_N_AXIS];
    motor_steps_to_mpos(print_position, probe_steps);

    log_stream(channel, "[PRB:" << report_util_axis_values(print_position) << ":" << probe_succeeded);
}

// Prints NGC parameters (coordinate offsets, probing)
void report_g92(Channel& channel) {}
void report_tlo(Channel& channel) {}

void report_ngc_coord(CoordIndex coord, Channel& channel) {
    if (coord == CoordIndex::TLO) {  // Non-persistent tool length offset
        float tlo      = gc_state.tool_length_offset;
        int   decimals = 3;
        if (config->_reportInches) {
            tlo *= INCH_PER_MM;
            decimals = 4;
        }
        std::ostringstream msg;
        msg << std::fixed << std::setprecision(decimals) << tlo;
        log_stream(channel, "[TLO:" << msg.str());
        return;
    }
    if (coord == CoordIndex::G92) {  // Non-persistent G92 offset
        log_stream(channel, "[G92:" << report_util_axis_values(gc_state.coord_offset));
        return;
    }
    // Persistent offsets G54 - G59, G28, and G30
    std::string name(coords[coord]->getName());
    name += ":";
    log_stream(channel, "[" << name << report_util_axis_values(coords[coord]->get()));
}
void report_ngc_parameters(Channel& channel) {
    for (auto coord = CoordIndex::Begin; coord < CoordIndex::End; ++coord) {
        report_ngc_coord(coord, channel);
    }
}

// Print current gcode parser mode state
void report_gcode_modes(Channel& channel) {
    std::ostringstream msg;
    switch (gc_state.modal.motion) {
        case Motion::None:
            msg << "G80";
            break;
        case Motion::Seek:
            msg << "G0";
            break;
        case Motion::Linear:
            msg << "G1";
            break;
        case Motion::CwArc:
            msg << "G2";
            break;
        case Motion::CcwArc:
            msg << "G3";
            break;
        case Motion::ProbeToward:
            msg << "G38.2";
            break;
        case Motion::ProbeTowardNoError:
            msg << "G38.3";
            break;
        case Motion::ProbeAway:
            msg << "G38.4";
            break;
        case Motion::ProbeAwayNoError:
            msg << "G38.5";
            break;
    }

    msg << " G" << (gc_state.modal.coord_select + 54);

    switch (gc_state.modal.plane_select) {
        case Plane::XY:
            msg << " G17";
            break;
        case Plane::ZX:
            msg << " G18";
            break;
        case Plane::YZ:
            msg << " G19";
            break;
    }

    switch (gc_state.modal.units) {
        case Units::Inches:
            msg << " G20";
            break;
        case Units::Mm:
            msg << " G21";
            break;
    }

    switch (gc_state.modal.distance) {
        case Distance::Absolute:
            msg << " G90";
            break;
        case Distance::Incremental:
            msg << " G91";
            break;
    }

#if 0
    switch (gc_state.modal.arc_distance) {
        case ArcDistance::Absolute: msg << " G90.1"; break;
        case ArcDistance::Incremental: msg << " G91.1"; break;
    }
#endif

    switch (gc_state.modal.feed_rate) {
        case FeedRate::UnitsPerMin:
            msg << " G94";
            break;
        case FeedRate::InverseTime:
            msg << " G93";
            break;
    }

    //report_util_gcode_modes_M();
    switch (gc_state.modal.program_flow) {
        case ProgramFlow::Running:
            msg << "";
            break;
        case ProgramFlow::Paused:
            msg << " M0";
            break;
        case ProgramFlow::OptionalStop:
            msg << " M1";
            break;
        case ProgramFlow::CompletedM2:
            msg << " M2";
            break;
        case ProgramFlow::CompletedM30:
            msg << " M30";
            break;
    }

    switch (gc_state.modal.spindle) {
        case SpindleState::Cw:
            msg << " M3";
            break;
        case SpindleState::Ccw:
            msg << " M4";
            break;
        case SpindleState::Disable:
            msg << " M5";
            break;
        default:
            break;
    }

    //report_util_gcode_modes_M();  // optional M7 and M8 should have been dealt with by here
    auto coolant = gc_state.modal.coolant;
    if (!coolant.Mist && !coolant.Flood) {
        msg << " M9";
    } else {
        // Note: Multiple coolant states may be active at the same time.
        if (coolant.Mist) {
            msg << " M7";
        }
        if (coolant.Flood) {
            msg << " M8";
        }
    }

    if (config->_enableParkingOverrideControl && sys.override_ctrl == Override::ParkingMotion) {
        msg << " M56";
    }

    msg << " T" << gc_state.tool;
    int digits = config->_reportInches ? 1 : 0;
    msg << " F" << std::fixed << std::setprecision(digits) << gc_state.feed_rate;
    msg << " S" << uint32_t(gc_state.spindle_speed);
    log_stream(channel, "[GC:" << msg.str())
}

// Prints build info line
void report_build_info(const char* line, Channel& channel) {
    log_stream(channel, "[VER:" << grbl_version << " FluidNC " << git_info << ":" << line);

    // The option message is included for backwards compatibility but
    // is not particularly useful for FluidNC, which has runtime
    // configuration and many more options than could reasonably
    // be listed via a string of characters.
    std::string msg;
    if (config->_coolant->hasMist()) {
        msg += "M";
    }
    msg += "PH";
    if (ALLOW_FEED_OVERRIDE_DURING_PROBE_CYCLES) {
        msg += "A";
    }
#ifdef ENABLE_BLUETOOTH
    if (WebUI::bt_enable->get()) {
        msg += "B";
    }
#endif
    msg += "S";
    if (config->_enableParkingOverrideControl) {
        msg += "R";
    }
    if (!FORCE_BUFFER_SYNC_DURING_NVS_WRITE) {
        msg += "E";  // Shown when disabled
    }
    if (!FORCE_BUFFER_SYNC_DURING_WCO_CHANGE) {
        msg += "W";  // Shown when disabled.
    }
    log_stream(channel, "[OPT:" << msg);

    log_msg_to(channel, "Machine: " << config->_name);

    std::string station_info = WebUI::wifi_config.station_info();
    if (station_info.length()) {
        log_msg_to(channel, station_info);
    }
    std::string ap_info = WebUI::wifi_config.ap_info();
    if (ap_info.length()) {
        log_msg_to(channel, ap_info);
    }
    if (!station_info.length() && !ap_info.length()) {
        log_msg_to(channel, "No Wifi");
    }
    std::string bt_info = WebUI::bt_config.info();
    if (bt_info.length()) {
        log_msg_to(channel, bt_info);
    }
}

// Prints the character string line that was received, which has been pre-parsed,
// and has been sent into protocol_execute_line() routine to be executed.
void report_echo_line_received(char* line, Channel& channel) {
    log_stream(channel, "[echo: " << line);
}

// Calculate the position for status reports.
// float print_position = returned position
// float wco            = returns the work coordinate offset
// bool wpos            = true for work position compensation

void addPinReport(char* status, char pinLetter) {
    size_t pos      = strlen(status);
    status[pos]     = pinLetter;
    status[pos + 1] = '\0';
}

void mpos_to_wpos(float* position) {
    float* wco    = get_wco();
    auto   n_axis = config->_axes->_numberAxis;
    for (int idx = 0; idx < n_axis; idx++) {
        position[idx] -= wco[idx];
    }
}

const char* state_name() {
    switch (sys.state) {
        case State::Idle:
            return "Idle";
        case State::Cycle:
            return "Run";
        case State::Hold:
            if (!(sys.suspend.bit.jogCancel)) {
                return sys.suspend.bit.holdComplete ? "Hold:0" : "Hold:1";
            }  // Continues to print jog state during jog cancel.
        case State::Jog:
            return "Jog";
        case State::Homing:
            return "Home";
        case State::Critical:
        case State::ConfigAlarm:
        case State::Alarm:
            return "Alarm";
        case State::CheckMode:
            return "Check";
        case State::SafetyDoor:
            if (sys.suspend.bit.initiateRestore) {
                return "Door:3";  // Restoring
            }
            if (sys.suspend.bit.retractComplete) {
                return sys.suspend.bit.safetyDoorAjar ? "Door:1" : "Door:0";
                // Door:0 means door closed and ready to resume
            }
            return "Door:2";  // Retracting
        case State::Sleep:
            return "Sleep";
    }
    return "";
}

void report_recompute_pin_string() {
    report_pin_string = "";
    if (config->_probe->get_state()) {
        report_pin_string += 'P';
    }

    MotorMask lim_pin_state = limits_get_state();
    if (lim_pin_state) {
        auto n_axis = config->_axes->_numberAxis;
        for (size_t axis = 0; axis < n_axis; axis++) {
            if (bitnum_is_true(lim_pin_state, Machine::Axes::motor_bit(axis, 0)) ||
                bitnum_is_true(lim_pin_state, Machine::Axes::motor_bit(axis, 1))) {
                report_pin_string += config->_axes->axisName(axis);
            }
        }
    }

    std::string ctrl_pin_report = config->_control->report_status();
    if (ctrl_pin_report.length()) {
        report_pin_string += ctrl_pin_report;
    }
}

// Define this to do something if a debug request comes in over serial
void report_realtime_debug() {}

// Prints real-time data. This function grabs a real-time snapshot of the stepper subprogram
// and the actual location of the CNC machine. Users may change the following function to their
// specific needs, but the desired real-time data report must be as short as possible. This is
// requires as it minimizes the computational overhead to keep running smoothly,
// especially during g-code programs with fast, short line segments and high frequency reports (5-20Hz).
void report_realtime_status(Channel& channel) {
    LogStream msg(channel, "<");
    msg << state_name();

    // Report position
    float* print_position = get_mpos();
    if (bits_are_true(status_mask->get(), RtStatus::Position)) {
        msg << "|MPos:";
    } else {
        msg << "|WPos:";
        mpos_to_wpos(print_position);
    }
    msg << report_util_axis_values(print_position).c_str();

    // Returns planner and serial read buffer states.

    if (bits_are_true(status_mask->get(), RtStatus::Buffer)) {
        msg << "|Bf:" << plan_get_block_buffer_available() << "," << channel.rx_buffer_available();
    }

    if (config->_useLineNumbers) {
        // Report current line number
        plan_block_t* cur_block = plan_get_current_block();
        if (cur_block != NULL) {
            uint32_t ln = cur_block->line_number;
            if (ln > 0) {
                msg << "|Ln:" << ln;
            }
        }
    }

    // Report realtime feed speed
    float rate = Stepper::get_realtime_rate();
    if (config->_reportInches) {
        rate /= MM_PER_INCH;
    }
    msg << "|FS:" << setprecision(0) << rate << "," << sys.spindle_speed;

    if (report_pin_string.length()) {
        msg << "|Pn:" << report_pin_string;
    }

    if (report_wco_counter > 0) {
        report_wco_counter--;
    } else {
        switch (sys.state) {
            case State::Homing:
            case State::Cycle:
            case State::Hold:
            case State::Jog:
            case State::SafetyDoor:
                report_wco_counter = (REPORT_WCO_REFRESH_BUSY_COUNT - 1);  // Reset counter for slow refresh
            default:
                report_wco_counter = (REPORT_WCO_REFRESH_IDLE_COUNT - 1);
                break;
        }
        if (report_ovr_counter == 0) {
            report_ovr_counter = 1;  // Set override on next report.
        }
        msg << "|WCO:" << report_util_axis_values(get_wco()).c_str();
    }

    if (report_ovr_counter > 0) {
        report_ovr_counter--;
    } else {
        switch (sys.state) {
            case State::Homing:
            case State::Cycle:
            case State::Hold:
            case State::Jog:
            case State::SafetyDoor:
                report_ovr_counter = (REPORT_OVR_REFRESH_BUSY_COUNT - 1);  // Reset counter for slow refresh
            default:
                report_ovr_counter = (REPORT_OVR_REFRESH_IDLE_COUNT - 1);
                break;
        }

        msg << "|Ov:" << int(sys.f_override) << "," << int(sys.r_override) << "," << int(sys.spindle_speed_ovr);
        SpindleState sp_state      = spindle->get_state();
        CoolantState coolant_state = config->_coolant->get_state();
        if (sp_state != SpindleState::Disable || coolant_state.Mist || coolant_state.Flood) {
            msg << "|A:";
            switch (sp_state) {
                case SpindleState::Disable:
                    break;
                case SpindleState::Cw:
                    msg << "S";
                    break;
                case SpindleState::Ccw:
                    msg << "C";
                    break;
                case SpindleState::Unknown:
                    break;
            }

            auto coolant = coolant_state;
            if (coolant.Flood) {
                msg << "F";
            }
            if (coolant.Mist) {
                msg << "M";
            }
        }
    }
    if (InputFile::_progress.length()) {
        msg << "|" + InputFile::_progress;
    }
#ifdef DEBUG_STEPPER_ISR
    msg << "|ISRs:" << Stepper::isr_count;
#endif
#ifdef DEBUG_REPORT_HEAP
    msg << "|Heap:" << xPortGetFreeHeapSize();
#endif
    msg << ">";
    // The destructor sends the line when msg goes out of scope
}

void hex_msg(uint8_t* buf, const char* prefix, int len) {
    char report[200];
    char temp[20];
    sprintf(report, "%s", prefix);
    for (int i = 0; i < len; i++) {
        sprintf(temp, " 0x%02X", buf[i]);
        strcat(report, temp);
    }

    log_info(report);
}

void reportTaskStackSize(UBaseType_t& saved) {
#ifdef DEBUG_REPORT_STACK_FREE
    UBaseType_t newHighWater = uxTaskGetStackHighWaterMark(NULL);
    if (newHighWater != saved) {
        saved = newHighWater;
        log_debug(pcTaskGetTaskName(NULL) << " Min Stack Space:" << saved);
    }
#endif
}

void WEAK_LINK display_init() {}
