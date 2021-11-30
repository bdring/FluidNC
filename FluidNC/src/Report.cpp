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
#include "WebUI/NotificationsService.h"  // WebUI::notificationsservice
#include "WebUI/WifiConfig.h"            // wifi_config
#include "WebUI/TelnetServer.h"          // WebUI::telnet_server
#include "WebUI/BTConfig.h"              // bt_config
#include "WebUI/WebSettings.h"
#include "InputFile.h"

#include <map>
#include <freertos/task.h>
#include <cstring>
#include <cstdio>
#include <cstdarg>

#ifdef DEBUG_REPORT_HEAP
EspClass esp;
#endif

portMUX_TYPE mmux = portMUX_INITIALIZER_UNLOCKED;

void _notify(const char* title, const char* msg) {
    WebUI::notificationsservice.sendMSG(title, msg);
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
static void report_util_axis_values(const float* axis_value, Print& channel) {
    float unit_conv = 1.0;  // unit conversion multiplier..default is mm
    int   decimals  = 3;    // Default - report mm to 3 decimal places
    if (config->_reportInches) {
        unit_conv = 1.0f / MM_PER_INCH;
        decimals  = 4;  // Report inches to 4 decimal places
    }
    auto n_axis = config->_axes->_numberAxis;
    for (size_t idx = 0; idx < n_axis; idx++) {
        channel << setprecision(decimals) << (axis_value[idx] * unit_conv);
        if (idx < (n_axis - 1)) {
            channel << ",";
        }
    }
}

// Handles the primary confirmation protocol response for streaming interfaces and human-feedback.
// For every incoming line, this method responds with an 'ok' for a successful command or an
// 'error:'  to indicate some error event with the line or some critical system error during
// operation. Errors events can originate from the g-code parser, settings module, or asynchronously
// from a critical error, such as a triggered hard limit. Interface should always monitor for these
// responses.
bool readyNext = false;
void report_status_message(Error status_code, Channel& channel) {
    if (infile) {
        // When running from a file, the GCode is not coming from a sender, so we are not
        // using the Grbl send/response/error protocol.  We set the readyNext flag instead
        // sending "ok" to indicate readiness for another line.  We report verbose error
        // messages with [MSG: ...] encapsulation
        switch (status_code) {
            case Error::Ok:
                readyNext = true;  // flag so protocol_main_loop() will send the next line
                break;
            default:
                infile->getChannel() << "[MSG: ERR:" << static_cast<int>(status_code) << " (" << errorString(status_code) << ") in "
                                     << infile->path() << " at line " << infile->getLineNumber() << "]\n";
                if (status_code == Error::GcodeUnsupportedCommand) {
                    // Do not stop on unsupported commands because most senders do not
                    readyNext = true;
                } else {
                    // Stop the file job on other errors
                    _notifyf("File job error", "Error:%d in %s at line: %d", status_code, infile->path(), infile->getLineNumber());
                    delete infile;
                    infile = nullptr;
                }
        }
    } else {
        // Input is coming from a sender so use the classic Grbl line protocol
        channel.ack(status_code);
    }
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

#include "Uart.h"
// Welcome message
void report_init_message(Print& channel) {
    channel << "\r\nGrbl " << grbl_version << " [FluidNC " << git_info << " (";

#if defined(ENABLE_WIFI) || defined(ENABLE_BLUETOOTH)
#    ifdef ENABLE_WIFI
    channel << "wifi";
#    endif

#    ifdef ENABLE_BLUETOOTH
    channel << "bt";
#    endif
#else
    channel << "noradio";
#endif

    channel << ") '$' for help]\n";
}

// Prints current probe parameters. Upon a probe command, these parameters are updated upon a
// successful probe or upon a failed probe with the G38.3 without errors command (if supported).
// These values are retained until the system is power-cycled, whereby they will be re-zeroed.
void report_probe_parameters(Print& channel) {
    // Report in terms of machine position.
    // get the machine position and put them into a string and append to the probe report
    float print_position[MAX_N_AXIS];
    motor_steps_to_mpos(print_position, probe_steps);
    channel << "[PRB:";
    report_util_axis_values(print_position, channel);
    channel << ":" << probe_succeeded << "]\n";
}

// Prints NGC parameters (coordinate offsets, probing)
void report_ngc_parameters(Print& channel) {
    // Print persistent offsets G54 - G59, G28, and G30
    for (auto coord_select = CoordIndex::Begin; coord_select < CoordIndex::End; ++coord_select) {
        channel << '[' << coords[coord_select]->getName() << ":";
        report_util_axis_values(coords[coord_select]->get(), channel);
        channel << "]\n";
    }
    // Print non-persistent G92,G92.1
    channel << "[G92:";
    report_util_axis_values(gc_state.coord_offset, channel);
    channel << "]\n";
    // Print tool length offset
    channel << "[TLO:";
    float tlo = gc_state.tool_length_offset;
    if (config->_reportInches) {
        tlo *= INCH_PER_MM;
    }
    channel << setprecision(3) << tlo << "]\n";
    if (probe_succeeded) {
        report_probe_parameters(channel);
    }
}

// Print current gcode parser mode state
void report_gcode_modes(Print& channel) {
    channel << "[GC:";
    const char* mode = "";

    switch (gc_state.modal.motion) {
        case Motion::None:
            mode = "G80";
            break;
        case Motion::Seek:
            mode = "G0";
            break;
        case Motion::Linear:
            mode = "G1";
            break;
        case Motion::CwArc:
            mode = "G2";
            break;
        case Motion::CcwArc:
            mode = "G3";
            break;
        case Motion::ProbeToward:
            mode = "G38.1";
            break;
        case Motion::ProbeTowardNoError:
            mode = "G38.2";
            break;
        case Motion::ProbeAway:
            mode = "G38.3";
            break;
        case Motion::ProbeAwayNoError:
            mode = "G38.4";
            break;
    }
    channel << mode;

    channel << " G" << (gc_state.modal.coord_select + 54);

    switch (gc_state.modal.plane_select) {
        case Plane::XY:
            mode = " G17";
            break;
        case Plane::ZX:
            mode = " G18";
            break;
        case Plane::YZ:
            mode = " G19";
            break;
    }
    channel << mode;

    switch (gc_state.modal.units) {
        case Units::Inches:
            mode = " G20";
            break;
        case Units::Mm:
            mode = " G21";
            break;
    }
    channel << mode;

    switch (gc_state.modal.distance) {
        case Distance::Absolute:
            mode = " G90";
            break;
        case Distance::Incremental:
            mode = " G91";
            break;
    }
    channel << mode;

#if 0
    switch (gc_state.modal.arc_distance) {
        case ArcDistance::Absolute: mode = " G90.1"; break;
        case ArcDistance::Incremental: mode = " G91.1"; break;
    }
    channel << mode;
#endif

    switch (gc_state.modal.feed_rate) {
        case FeedRate::UnitsPerMin:
            mode = " G94";
            break;
        case FeedRate::InverseTime:
            mode = " G93";
            break;
    }
    channel << mode;

    //report_util_gcode_modes_M();
    switch (gc_state.modal.program_flow) {
        case ProgramFlow::Running:
            mode = "";
            break;
        case ProgramFlow::Paused:
            mode = " M0";
            break;
        case ProgramFlow::OptionalStop:
            mode = " M1";
            break;
        case ProgramFlow::CompletedM2:
            mode = " M2";
            break;
        case ProgramFlow::CompletedM30:
            mode = " M30";
            break;
    }
    channel << mode;

    switch (gc_state.modal.spindle) {
        case SpindleState::Cw:
            mode = " M3";
            break;
        case SpindleState::Ccw:
            mode = " M4";
            break;
        case SpindleState::Disable:
            mode = " M5";
            break;
        default:
            mode = "";
    }
    channel << mode;

    //report_util_gcode_modes_M();  // optional M7 and M8 should have been dealt with by here
    auto coolant = gc_state.modal.coolant;
    if (!coolant.Mist && !coolant.Flood) {
        channel << " M9";
    } else {
        // Note: Multiple coolant states may be active at the same time.
        if (coolant.Mist) {
            channel << " M7";
        }
        if (coolant.Flood) {
            channel << " M8";
        }
    }

    if (config->_enableParkingOverrideControl && sys.override_ctrl == Override::ParkingMotion) {
        channel << " M56";
    }

    channel << " T" << gc_state.tool;
    int digits = config->_reportInches ? 1 : 0;
    channel << " F" << setprecision(digits) << gc_state.feed_rate;
    channel << " S" << uint32_t(gc_state.spindle_speed);
    channel << "]\n";
}

// Prints build info line
void report_build_info(const char* line, Print& channel) {
    channel << "[VER:" << grbl_version << " FluidNC " << git_info << ":" << line << "]\n";
    channel << "[OPT:";
    if (config->_coolant->hasMist()) {
        channel << "M";  // TODO Need to deal with M8...it could be disabled
    }
    channel << "PH";
    if (ALLOW_FEED_OVERRIDE_DURING_PROBE_CYCLES) {
        channel << "A";
    }
#ifdef ENABLE_BLUETOOTH
    if (WebUI::bt_enable->get()) {
        channel << "B";
    }
#endif
    channel << "S";
    if (config->_enableParkingOverrideControl) {
        channel << "R";
    }
    if (!FORCE_BUFFER_SYNC_DURING_NVS_WRITE) {
        channel << "E";  // Shown when disabled
    }
    if (!FORCE_BUFFER_SYNC_DURING_WCO_CHANGE) {
        channel << "W";  // Shown when disabled.
    }
    // NOTE: Compiled values, like override increments/max/min values, may be added at some point later.
    // These will likely have a comma delimiter to separate them.
    channel << "]\n";

    channel << "[MSG: Machine: " << config->_name << "]\n";

    String info = WebUI::wifi_config.info();
    if (info.length()) {
        channel << "[MSG: " << info << "]\n";
    }
    info = WebUI::bt_config.info();
    if (info.length()) {
        channel << "[MSG: " << info << "]\n";
    }
}

// Prints the character string line that was received, which has been pre-parsed,
// and has been sent into protocol_execute_line() routine to be executed.
void report_echo_line_received(char* line, Print& channel) {
    channel << "[echo: " << line << "]\n";
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

static float* get_wco() {
    static float wco[MAX_N_AXIS];
    auto         n_axis = config->_axes->_numberAxis;
    for (int idx = 0; idx < n_axis; idx++) {
        // Apply work coordinate offsets and tool length offset to current position.
        wco[idx] = gc_state.coord_system[idx] + gc_state.coord_offset[idx];
        if (idx == TOOL_LENGTH_OFFSET_AXIS) {
            wco[idx] += gc_state.tool_length_offset;
        }
    }
    return wco;
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

static void pinString(Print& channel) {
    bool prefixNeeded = true;
    if (config->_probe->get_state()) {
        if (prefixNeeded) {
            prefixNeeded = false;
            channel << "|Pn:";
        }
        channel << 'P';
    }

    MotorMask lim_pin_state = limits_get_state();
    if (lim_pin_state) {
        auto n_axis = config->_axes->_numberAxis;
        for (int i = 0; i < n_axis; i++) {
            if (bitnum_is_true(lim_pin_state, i) || bitnum_is_true(lim_pin_state, i + 16)) {
                if (prefixNeeded) {
                    prefixNeeded = false;
                    channel << "|Pn:";
                }
                channel << config->_axes->axisName(i);
            }
        }
    }

    channel << config->_control->report();
}

// Prints real-time data. This function grabs a real-time snapshot of the stepper subprogram
// and the actual location of the CNC machine. Users may change the following function to their
// specific needs, but the desired real-time data report must be as short as possible. This is
// requires as it minimizes the computational overhead to keep running smoothly,
// especially during g-code programs with fast, short line segments and high frequency reports (5-20Hz).
void report_realtime_status(Channel& channel) {
    channel << "<" << state_name();

    // Report position
    float* print_position = get_mpos();
    if (bits_are_true(status_mask->get(), RtStatus::Position)) {
        channel << "|MPos:";
    } else {
        channel << "|WPos:";
        mpos_to_wpos(print_position);
    }
    report_util_axis_values(print_position, channel);

    // Returns planner and serial read buffer states.

    if (bits_are_true(status_mask->get(), RtStatus::Buffer)) {
        channel << "|Bf:" << plan_get_block_buffer_available() << "," << channel.rx_buffer_available();
    }

    if (config->_useLineNumbers) {
        // Report current line number
        plan_block_t* cur_block = plan_get_current_block();
        if (cur_block != NULL) {
            uint32_t ln = cur_block->line_number;
            if (ln > 0) {
                channel << "|Ln:" << ln;
            }
        }
    }

    // Report realtime feed speed
    float rate = Stepper::get_realtime_rate();
    if (config->_reportInches) {
        rate /= MM_PER_INCH;
    }
    channel << "|FS:" << setprecision(0) << rate << "," << sys.spindle_speed;

    pinString(channel);

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
        channel << "|WCO:";
        report_util_axis_values(get_wco(), channel);
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

        channel << "|Ov:" << sys.f_override << "," << sys.r_override << "," << sys.spindle_speed_ovr;
        SpindleState sp_state      = spindle->get_state();
        CoolantState coolant_state = config->_coolant->get_state();
        if (sp_state != SpindleState::Disable || coolant_state.Mist || coolant_state.Flood) {
            channel << "|A:";
            switch (sp_state) {
                case SpindleState::Disable:
                    break;
                case SpindleState::Cw:
                    channel << "S";
                    break;
                case SpindleState::Ccw:
                    channel << "C";
                    break;
                case SpindleState::Unknown:
                    break;
            }

            auto coolant = coolant_state;
            // XXX WMB why .Flood in one case and ->hasMist() in the other? also see above
            if (coolant.Flood) {
                channel << "F";
            }
            if (config->_coolant->hasMist()) {
                channel << "M";
            }
        }
    }
    if (infile) {
        channel << "|SD:" << setprecision(2) << infile->percent_complete() << "," << infile->path();
    }
#ifdef DEBUG_STEPPER_ISR
    channel << "|ISRs:" << config->_stepping->isr_count;
#endif
#ifdef DEBUG_REPORT_HEAP
    channel << "|Heap:" << esp.getHeapSize();
#endif
    channel << ">\n";
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
void WEAK_LINK display(const char* tag, String s) {}
