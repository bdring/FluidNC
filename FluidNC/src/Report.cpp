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
#include "WebUI/NotificationsService.h"  // WebUI::notificationsservice
#include "WebUI/WifiConfig.h"            // wifi_config
#include "WebUI/TelnetServer.h"          // WebUI::telnet_server
#include "WebUI/BTConfig.h"              // bt_config
#include "WebUI/WebSettings.h"

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

// formats axis values into a string and returns that string in rpt
// NOTE: rpt should have at least size: axesStringLen
static void report_util_axis_values(float* axis_value, char* rpt) {
    char        axisVal[coordStringLen];
    float       unit_conv = 1.0;      // unit conversion multiplier..default is mm
    const char* format    = "%4.3f";  // Default - report mm to 3 decimal places
    rpt[0]                = '\0';
    if (config->_reportInches) {
        unit_conv = 1.0f / MM_PER_INCH;
        format    = "%4.4f";  // Report inches to 4 decimal places
    }
    auto n_axis = config->_axes->_numberAxis;
    for (size_t idx = 0; idx < n_axis; idx++) {
        snprintf(axisVal, coordStringLen - 1, format, axis_value[idx] * unit_conv);
        strcat(rpt, axisVal);
        if (idx < (n_axis - 1)) {
            strcat(rpt, ",");
        }
    }
}

// This version returns the axis values as a String
static String report_util_axis_values(const float* axis_value) {
    String rpt       = "";
    float  unit_conv = 1.0;  // unit conversion multiplier..default is mm
    int    decimals  = 3;    // Default - report mm to 3 decimal places
    if (config->_reportInches) {
        unit_conv = 1.0f / MM_PER_INCH;
        decimals  = 4;  // Report inches to 4 decimal places
    }
    auto n_axis = config->_axes->_numberAxis;
    for (size_t idx = 0; idx < n_axis; idx++) {
        rpt += String(axis_value[idx] * unit_conv, decimals);
        if (idx < (n_axis - 1)) {
            rpt += ",";
        }
    }
    return rpt;
}

// Handles the primary confirmation protocol response for streaming interfaces and human-feedback.
// For every incoming line, this method responds with an 'ok' for a successful command or an
// 'error:'  to indicate some error event with the line or some critical system error during
// operation. Errors events can originate from the g-code parser, settings module, or asynchronously
// from a critical error, such as a triggered hard limit. Interface should always monitor for these
// responses.
void report_status_message(Error status_code, Print& client) {
    auto sdcard = config->_sdCard;
    if (sdcard->get_state() == SDCard::State::BusyPrinting) {
        // When running from SD, the GCode is not coming from a sender, so we are not
        // using the Grbl send/response/error protocol.  We use _readyNext instead of
        // "ok" to indicate readiness for another line, and we report verbose error
        // messages with [MSG: ...] encapsulation
        switch (status_code) {
            case Error::Ok:
                sdcard->_readyNext = true;  // flag so protocol_main_loop() will send the next line
                break;
            case Error::Eof:
                // XXX we really should wait for the machine to return to idle before
                // we issue this message.  What Eof really means is that all the lines in the
                // file were sent, but not necessarily executed.  Some could still be running.
                _notifyf("SD print done", "%s print succeeded", sdcard->filename());
                sdcard->getClient() << "[MSG:" << sdcard->filename() << " print succeeded]\n";
                sdcard->closeFile();
                break;
            default:
                sdcard->getClient() << "[MSG: ERR:" << static_cast<int>(status_code) << " (" << errorString(status_code) << ") in "
                                    << sdcard->filename() << " at line " << sdcard->lineNumber() << "]\n";
                if (status_code == Error::GcodeUnsupportedCommand) {
                    // Do not stop on unsupported commands because most senders do not
                    sdcard->_readyNext = true;
                } else {
                    _notifyf("SD print error", "Error:%d in %s at line: %d", status_code, sdcard->filename(), sdcard->lineNumber());
                    sdcard->closeFile();
                }
        }
    } else {
        // Input is coming from a sender so use the classic Grbl line protocol
        switch (status_code) {
            case Error::Ok:  // Error::Ok
                client << "ok\n";
                break;
            default:
                // With verbose errors, the message text is displayed instead of the number.
                // Grbl 0.9 used to display the text, while Grbl 1.1 switched to the number.
                // Many senders support both formats.
                client << "error:";
                if (config->_verboseErrors) {
                    client << errorString(status_code);
                } else {
                    client << static_cast<int>(status_code);
                }
                client << '\n';
                break;
        }
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
    // { Message::SdFileQuit, "Reset during SD file at line: %d" },
};

// Prints feedback messages. This serves as a centralized method to provide additional
// user feedback for things that are not of the status/alarm message protocol. These are
// messages such as setup warnings, switch toggling, and how to exit alarms.
// NOTE: For interfaces, messages are always placed within brackets. And if silent mode
// is installed, the message number codes are less than zero.
void report_feedback_message(Message message) {  // ok to send to all clients
    auto it = MessageText.find(message);
    if (it != MessageText.end()) {
        log_info(it->second);
    }
}

#include "Uart.h"
// Welcome message
void report_init_message(Print& client) {
    client << "\r\nGrbl " << grbl_version << " [FluidNC " << git_info << " (";

#if defined(ENABLE_WIFI) || defined(ENABLE_BLUETOOTH)
#    ifdef ENABLE_WIFI
    client << "wifi";
#    endif

#    ifdef ENABLE_BLUETOOTH
    client << "bt";
#    endif
#else
    client << "noradio";
#endif

    client << ") '$' for help]\n";
}

// Prints current probe parameters. Upon a probe command, these parameters are updated upon a
// successful probe or upon a failed probe with the G38.3 without errors command (if supported).
// These values are retained until the system is power-cycled, whereby they will be re-zeroed.
void report_probe_parameters(Print& client) {
    // Report in terms of machine position.
    // get the machine position and put them into a string and append to the probe report
    float print_position[MAX_N_AXIS];
    motor_steps_to_mpos(print_position, probe_steps);
    client << "[PRB:" << report_util_axis_values(print_position) << ":" << probe_succeeded << '\n';
}

// Prints NGC parameters (coordinate offsets, probing)
void report_ngc_parameters(Print& client) {
    // Print persistent offsets G54 - G59, G28, and G30
    for (auto coord_select = CoordIndex::Begin; coord_select < CoordIndex::End; ++coord_select) {
        client << '[' << coords[coord_select]->getName() << ":";
        client << report_util_axis_values(coords[coord_select]->get());
        client << '\n';
    }
    // Print non-persistent G92,G92.1
    client << "[G92:" << report_util_axis_values(gc_state.coord_offset) << "]\n";
    // Print tool length offset
    client << "[TLO:";
    float tlo = gc_state.tool_length_offset;
    if (config->_reportInches) {
        tlo *= INCH_PER_MM;
    }
    client << String(tlo, 3) << "]\n";
    report_probe_parameters(client);
}

// Print current gcode parser mode state
void report_gcode_modes(Print& client) {
    client << "[GC:";
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
    client << mode;

    client << " G" << (gc_state.modal.coord_select + 54);

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
    client << mode;

    switch (gc_state.modal.units) {
        case Units::Inches:
            mode = " G20";
            break;
        case Units::Mm:
            mode = " G21";
            break;
    }
    client << mode;

    switch (gc_state.modal.distance) {
        case Distance::Absolute:
            mode = " G90";
            break;
        case Distance::Incremental:
            mode = " G91";
            break;
    }
    client << mode;

#if 0
    switch (gc_state.modal.arc_distance) {
        case ArcDistance::Absolute: mode = " G90.1"; break;
        case ArcDistance::Incremental: mode = " G91.1"; break;
    }
    client << mode;
#endif

    switch (gc_state.modal.feed_rate) {
        case FeedRate::UnitsPerMin:
            mode = " G94";
            break;
        case FeedRate::InverseTime:
            mode = " G93";
            break;
    }
    client << mode;

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
    client << mode;

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
    client << mode;

    //report_util_gcode_modes_M();  // optional M7 and M8 should have been dealt with by here
    auto coolant = gc_state.modal.coolant;
    if (!coolant.Mist && !coolant.Flood) {
        client << " M9";
    } else {
        // Note: Multiple coolant states may be active at the same time.
        if (coolant.Mist) {
            client << " M7";
        }
        if (coolant.Flood) {
            client << " M8";
        }
    }

    if (config->_enableParkingOverrideControl && sys.override_ctrl == Override::ParkingMotion) {
        client << " M56";
    }

    client << " T" << gc_state.tool;
    // XXX WMB format according to config->_reportInches ? %.1f : %.0f
    client << " F" << gc_state.feed_rate;
    client << " S" << uint32_t(gc_state.spindle_speed);
    client << "]\n";
}

// Prints build info line
void report_build_info(const char* line, Print& client) {
    client << "[VER:FluidNC " << git_info << ":" << line << "]\n";
    client << "[OPT:";
    if (config->_coolant->hasMist()) {
        client << "M";  // TODO Need to deal with M8...it could be disabled
    }
    client << "PH";
    if (ALLOW_FEED_OVERRIDE_DURING_PROBE_CYCLES) {
        client << "A";
    }
#ifdef ENABLE_BLUETOOTH
    if (WebUI::bt_enable->get()) {
        client << "B";
    }
#endif
    client << "S";
    if (config->_enableParkingOverrideControl) {
        client << "R";
    }
    if (!FORCE_BUFFER_SYNC_DURING_NVS_WRITE) {
        client << "E";  // Shown when disabled
    }
    if (!FORCE_BUFFER_SYNC_DURING_WCO_CHANGE) {
        client << "W";  // Shown when disabled.
    }
    // NOTE: Compiled values, like override increments/max/min values, may be added at some point later.
    // These will likely have a comma delimiter to separate them.
    client << "]\n";

    client << "[MSG: Machine: " << config->_name << "]\n";

#ifdef ENABLE_WIFI
    String info = WebUI::wifi_config.info();
    if (info.length()) {
        client << "[MSG: Machine: " << info << "]\n";
        ;
    }
#endif
#ifdef ENABLE_BLUETOOTH
    if (WebUI::bt_enable->get()) {
        client << "[MSG: Machine: " << WebUI::bt_config.info() << "]\n";
    }
#endif
}

// Prints the character string line that was received, which has been pre-parsed,
// and has been sent into protocol_execute_line() routine to be executed.
void report_echo_line_received(char* line, Print& client) {
    client << "[echo: " << line << "]\n";
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

String pinString() {
    String pins = "";

    if (config->_probe->get_state()) {
        pins += 'P';
    }

    MotorMask lim_pin_state = limits_get_state();
    if (lim_pin_state) {
        auto n_axis = config->_axes->_numberAxis;
        for (int i = 0; i < n_axis; i++) {
            if (bitnum_is_true(lim_pin_state, i) || bitnum_is_true(lim_pin_state, i + 16)) {
                pins += config->_axes->axisName(i);
            }
        }
    }

    pins += config->_control->report();
    return pins;
}

// Prints real-time data. This function grabs a real-time snapshot of the stepper subprogram
// and the actual location of the CNC machine. Users may change the following function to their
// specific needs, but the desired real-time data report must be as short as possible. This is
// requires as it minimizes the computational overhead to keep running smoothly,
// especially during g-code programs with fast, short line segments and high frequency reports (5-20Hz).
void report_realtime_status(Print& client) {
    client << "<" << state_name();

    // Report position
    float* print_position = get_mpos();
    if (bits_are_true(status_mask->get(), RtStatus::Position)) {
        client << "|MPos:";
    } else {
        client << "|WPos:";
        mpos_to_wpos(print_position);
    }
    client << report_util_axis_values(print_position);

    // Returns planner and serial read buffer states.
#if 0
    // XXX WMB problem with client_get_rx_buffer_available(client)
    if (bits_are_true(status_mask->get(), RtStatus::Buffer)) {
        client << "|Bf:" << plan_get_block_buffer_available() << "," << client_get_rx_buffer_available(client /* CLIENT_SERIAL ??? */);
    }
#endif

    if (config->_useLineNumbers) {
        // Report current line number
        plan_block_t* cur_block = plan_get_current_block();
        if (cur_block != NULL) {
            uint32_t ln = cur_block->line_number;
            if (ln > 0) {
                client << "|Ln:" << ln;
            }
        }
    }

    // Report realtime feed speed
    float rate = Stepper::get_realtime_rate();
    if (config->_reportInches) {
        rate /= MM_PER_INCH;
    }
    // XXX WMB rate %.0f
    client << "|FS:" << rate << "," << sys.spindle_speed;

    String pins = pinString();
    if (pins.length()) {
        client << "|Pn:" << pins;
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
        client << "|WCO:" << report_util_axis_values(get_wco());
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

        client << "|Ov:" << sys.f_override << "," << sys.r_override << "," << sys.spindle_speed_ovr;
        SpindleState sp_state      = spindle->get_state();
        CoolantState coolant_state = config->_coolant->get_state();
        if (sp_state != SpindleState::Disable || coolant_state.Mist || coolant_state.Flood) {
            client << "|A:";
            switch (sp_state) {
                case SpindleState::Disable:
                    break;
                case SpindleState::Cw:
                    client << "S";
                    break;
                case SpindleState::Ccw:
                    client << "C";
                    break;
                case SpindleState::Unknown:
                    break;
            }

            auto coolant = coolant_state;
            // XXX WMB why .Flood in one case and ->hasMist() in the other? also see above
            if (coolant.Flood) {
                client << "F";
            }
            if (config->_coolant->hasMist()) {
                client << "M";
            }
        }
    }
    if (config->_sdCard->get_state() == SDCard::State::BusyPrinting) {
        // XXX WMB FORMAT 4.2f
        client << "|SD:" << config->_sdCard->percent_complete() << "," << config->_sdCard->filename();
    }
#ifdef DEBUG_STEPPER_ISR
    client << "|ISRs:" << config->_stepping->isr_count;
#endif
#ifdef DEBUG_REPORT_HEAP
    client << "|Heap:" << esp.getHeapSize();
#endif
    client << ">\n";
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
const char* dataBeginMarker = "[MSG: BeginData]\n";
const char* dataEndMarker   = "[MSG: EndData]\n";
