// Copyright (c) 2020 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Settings.h"

#include "Machine/MachineConfig.h"
#include "Configuration/RuntimeSetting.h"
#include "Configuration/AfterParse.h"
#include "Configuration/Validator.h"
#include "Configuration/ParseException.h"
#include "Machine/Axes.h"
#include "Regex.h"
#include "WebUI/Authentication.h"
#include "WebUI/WifiConfig.h"
#include "Report.h"
#include "MotionControl.h"
#include "System.h"
#include "Limits.h"               // homingAxes
#include "SettingsDefinitions.h"  // build_info
#include "Protocol.h"             // LINE_BUFFER_SIZE
#include "UartChannel.h"          // Uart0.write()
#include "FileStream.h"           // FileStream()
#include "xmodem.h"               // xmodemReceive(), xmodemTransmit()
#include "StartupLog.h"           // startupLog
#include "Driver/fluidnc_gpio.h"  // gpio_dump()

#include "FluidPath.h"
#include "HashFS.h"

#include <cstring>
#include <map>
#include <filesystem>

// WG Readable and writable as guest
// WU Readable and writable as user and admin
// WA Readable as user and admin, writable as admin

static Error fakeMaxSpindleSpeed(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out);

// If authentication is disabled, auth_level will be LEVEL_ADMIN
static bool auth_failed(Word* w, const char* value, WebUI::AuthenticationLevel auth_level) {
    permissions_t permissions = w->getPermissions();
    switch (auth_level) {
        case WebUI::AuthenticationLevel::LEVEL_ADMIN:  // Admin can do anything
            return false;                              // Nothing is an Admin auth fail
        case WebUI::AuthenticationLevel::LEVEL_GUEST:  // Guest can only access open settings
            return permissions != WG;                  // Anything other than RG is Guest auth fail
        case WebUI::AuthenticationLevel::LEVEL_USER:   // User is complicated...
            if (!value) {                              // User can read anything
                return false;                          // No read is a User auth fail
            }
            return permissions == WA;  // User cannot write WA
        default:
            return true;
    }
}

// Replace GRBL realtime characters with the corresponding URI-style
// escape sequence.
static std::string uriEncodeGrblCharacters(const char* clear) {
    std::string escaped;
    char        c;
    while ((c = *clear++) != '\0') {
        switch (c) {
            case '%':  // The escape character itself
                escaped += "%25";
                break;
            case '!':  // Cmd::FeedHold
                escaped += "%21";
                break;
            case '?':  // Cmd::StatusReport
                escaped += "%3F";
                break;
            case '~':  // Cmd::CycleStart
                escaped += "%7E";
                break;
            default:
                escaped += c;
                break;
        }
    }
    return escaped;
}

// Replace URI-style escape sequences like %HH with the character
// corresponding to the hex number HH.  This works with any escaped
// characters, not only those that are special to Grbl
static char* uriDecode(const char* s) {
    const int   dlen = 100;
    static char decoded[dlen + 1];
    char*       out = decoded;
    char        c;
    while ((c = *s++) != '\0') {
        if (c == '%') {
            if (strlen(s) < 2) {
                log_error("Bad % encoding - too short");
                goto done;
            }
            char escstr[3];
            escstr[0] = *s++;
            escstr[1] = *s++;
            escstr[2] = '\0';
            char*   endptr;
            uint8_t esc = strtol(escstr, &endptr, 16);
            if (endptr != &escstr[2]) {
                log_error("Bad % encoding - not hex");
                goto done;
            }
            c = (char)esc;
        }
        if ((out - decoded) == dlen) {
            log_error("String value too long");
            goto done;
        }
        *out++ = c;
    }
done:
    *out = '\0';
    return decoded;
}

static void show_setting(const char* name, const char* value, const char* description, Channel& out) {
    LogStream s(out, "$");
    s << name << "=" << uriEncodeGrblCharacters(value);
    if (description) {
        s << "    ";
        s << description;
    }
}

void settings_restore(uint8_t restore_flag) {
    if (restore_flag & SettingsRestore::Wifi) {
        WebUI::wifi_config.reset_settings();
    }

    if (restore_flag & SettingsRestore::Defaults) {
        bool restore_startup = restore_flag & SettingsRestore::StartupLines;
        for (Setting* s : Setting::List) {
            if (!s->getDescription()) {
                const char* name = s->getName();
                if (restore_startup) {  // all settings get restored
                    s->setDefault();
                } else if ((strcmp(name, "Line0") != 0) && (strcmp(name, "Line1") != 0)) {  // non startup settings get restored
                    s->setDefault();
                }
            }
        }
        log_info("Settings reset done");
    }
    if (restore_flag & SettingsRestore::Parameters) {
        for (auto idx = CoordIndex::Begin; idx < CoordIndex::End; ++idx) {
            coords[idx]->setDefault();
        }
        coords[gc_state.modal.coord_select]->get(gc_state.coord_system);
        report_wco_counter = 0;  // force next report to include WCO
    }
    log_info("Position offsets reset done");
}

// Get settings values from non volatile storage into memory
static void load_settings() {
    for (Setting* s : Setting::List) {
        s->load();
    }
}

extern void make_settings();
extern void make_user_commands();

namespace WebUI {
    extern void make_web_settings();
}

void settings_init() {
    make_settings();
    WebUI::make_web_settings();
    load_settings();
}

static Error show_help(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    log_string(out, "HLP:$$ $+ $# $S $L $G $I $N $x=val $Nx=line $J=line $SLP $C $X $H $F $E=err ~ ! ? ctrl-x");
    return Error::Ok;
}

static Error report_gcode(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    report_gcode_modes(out);
    return Error::Ok;
}

static void show_settings(Channel& out, type_t type) {
    for (Setting* s : Setting::List) {
        if (s->getType() == type && s->getGrblName()) {
            // The following test could be expressed more succinctly with XOR,
            // but is arguably clearer when written out
            show_setting(s->getGrblName(), s->getCompatibleValue(), NULL, out);
        }
    }
    // need this per issue #1036
    fakeMaxSpindleSpeed(NULL, WebUI::AuthenticationLevel::LEVEL_ADMIN, out);
}
static Error report_normal_settings(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    show_settings(out, GRBL);  // GRBL non-axis settings
    return Error::Ok;
}
static Error list_grbl_names(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    for (Setting* setting : Setting::List) {
        const char* gn = setting->getGrblName();
        if (gn) {
            log_stream(out, "$" << gn << " => $" << setting->getName());
        }
    }
    return Error::Ok;
}
static Error list_settings(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    for (Setting* s : Setting::List) {
        const char* displayValue = auth_failed(s, value, auth_level) ? "<Authentication required>" : s->getStringValue();
        if (s->getType() != PIN) {
            show_setting(s->getName(), displayValue, NULL, out);
        }
    }
    return Error::Ok;
}
static Error list_changed_settings(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    for (Setting* s : Setting::List) {
        const char* value = s->getStringValue();
        if (!auth_failed(s, value, auth_level) && strcmp(value, s->getDefaultString())) {
            if (s->getType() != PIN) {
                show_setting(s->getName(), value, NULL, out);
            }
        }
    }
    log_string(out, "(Passwords not shown)");
    return Error::Ok;
}
static Error list_commands(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    for (Command* cp : Command::List) {
        const char* name    = cp->getName();
        const char* oldName = cp->getGrblName();
        LogStream   s(out, "$");
        s << name;
        if (oldName) {
            s << " or $" << oldName;
        }
        const char* description = cp->getDescription();
        if (description) {
            s << " =" << description;
        }
    }
    return Error::Ok;
}
static Error toggle_check_mode(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (sys.state == State::ConfigAlarm) {
        return Error::ConfigurationInvalid;
    }

    // Perform reset when toggling off. Check g-code mode should only work when
    // idle and ready, regardless of alarm locks. This is mainly to keep things
    // simple and consistent.
    if (sys.state == State::CheckMode) {
        report_feedback_message(Message::Disabled);
        sys.abort = true;
    } else {
        if (sys.state != State::Idle) {
            return Error::IdleError;  // Requires no alarm mode.
        }
        sys.state = State::CheckMode;
        report_feedback_message(Message::Enabled);
    }
    return Error::Ok;
}
static Error isStuck() {
    // Block if a control pin is stuck on
    if (config->_control->safety_door_ajar()) {
        send_alarm(ExecAlarm::ControlPin);
        return Error::CheckDoor;
    }
    if (config->_control->stuck()) {
        log_info("Control pins:" << config->_control->report_status());
        send_alarm(ExecAlarm::ControlPin);
        return Error::CheckControlPins;
    }
    return Error::Ok;
}
static Error disable_alarm_lock(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (sys.state == State::ConfigAlarm) {
        return Error::ConfigurationInvalid;
    }
    if (sys.state == State::Alarm) {
        Error err = isStuck();
        if (err != Error::Ok) {
            return err;
        }
        Homing::set_all_axes_homed();
        report_feedback_message(Message::AlarmUnlock);
        sys.state = State::Idle;
    }
    // Run the after_unlock macro even if no unlock was necessary
    config->_macros->_after_unlock.run();
    return Error::Ok;
}
static Error report_ngc(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    report_ngc_parameters(out);
    return Error::Ok;
}
static Error msg_to_uart0(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (value) {
        Channel* dest = allChannels.find("uart_channel0");
        if (dest) {
            log_msg_to(*dest, value);
        }
    }
    return Error::Ok;
}
static Error msg_to_uart1(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (value && config->_uart_channels[1]) {
        log_msg_to(*(config->_uart_channels[1]), value);
    }
    return Error::Ok;
}
static Error cmd_log_msg(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (value) {
        if (*value == '*') {
            log_msg(value + 1);
        } else {
            log_msg_to(out, value);
        }
    }
    return Error::Ok;
}
static Error cmd_log_error(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (value) {
        if (*value == '*') {
            log_error(value + 1);
        } else {
            log_error_to(out, value);
        }
    }
    return Error::Ok;
}
static Error cmd_log_warn(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (value) {
        if (*value == '*') {
            log_warn(value + 1);
        } else {
            log_warn_to(out, value);
        }
    }
    return Error::Ok;
}
static Error cmd_log_info(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (value) {
        if (*value == '*') {
            log_info(value + 1);
        } else {
            log_info_to(out, value);
        }
    }
    return Error::Ok;
}
static Error cmd_log_debug(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (value) {
        if (*value == '*') {
            log_debug(value + 1);
        } else {
            log_debug_to(out, value);
        }
    }
    return Error::Ok;
}
static Error cmd_log_verbose(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (value) {
        if (*value == '*') {
            log_verbose(value + 1);
        } else {
            log_verbose_to(out, value);
        }
    }
    return Error::Ok;
}
static Error home(AxisMask axisMask) {
    if (axisMask != Machine::Homing::AllCycles) {  // if not AllCycles we need to make sure the cycle is not prohibited
        // if there is a cycle it is the axis from $H<axis>
        auto n_axis = config->_axes->_numberAxis;
        for (int axis = 0; axis < n_axis; axis++) {
            if (bitnum_is_true(axisMask, axis)) {
                auto axisConfig     = config->_axes->_axis[axis];
                auto homing_allowed = axisConfig->_homing->_allow_single_axis;
                if (!homing_allowed)
                    return Error::SingleAxisHoming;
            }
        }
    }

    if (sys.state == State::ConfigAlarm) {
        return Error::ConfigurationInvalid;
    }
    if (!Machine::Axes::homingMask) {
        return Error::SettingDisabled;
    }

    if (config->_control->safety_door_ajar()) {
        return Error::CheckDoor;  // Block if safety door is ajar.
    }

    Machine::Homing::run_cycles(axisMask);

    do {
        protocol_execute_realtime();
    } while (sys.state == State::Homing);

    if (!Homing::unhomed_axes()) {
        config->_macros->_after_homing.run();
    }

    return Error::Ok;
}
static Error home_all(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    AxisMask requestedAxes = Machine::Homing::AllCycles;
    auto     retval        = Error::Ok;

    // value can be a list of cycle numbers like "21", which will run homing cycle 2 then cycle 1,
    // or a list of axis names like "XZ", which will home the X and Z axes simultaneously
    if (value) {
        int ndigits = 0;
        for (int i = 0; i < strlen(value); i++) {
            char cycleName = value[i];
            if (isdigit(cycleName)) {
                if (!Machine::Homing::axis_mask_from_cycle(cycleName - '0')) {
                    log_error("No axes for homing cycle " << cycleName);
                    return Error::InvalidValue;
                }
                ++ndigits;
            }
        }
        if (ndigits) {
            if (ndigits != strlen(value)) {
                log_error("Invalid homing cycle list");
                return Error::InvalidValue;
            } else {
                for (int i = 0; i < strlen(value); i++) {
                    char cycleName = value[i];
                    requestedAxes  = Machine::Homing::axis_mask_from_cycle(cycleName - '0');
                    retval         = home(requestedAxes);
                    if (retval != Error::Ok) {
                        return retval;
                    }
                }
                return retval;
            }
        }
        if (!config->_axes->namesToMask(value, requestedAxes)) {
            return Error::InvalidValue;
        }
    }

    return home(requestedAxes);
}

static Error home_x(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    return home(bitnum_to_mask(X_AXIS));
}
static Error home_y(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    return home(bitnum_to_mask(Y_AXIS));
}
static Error home_z(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    return home(bitnum_to_mask(Z_AXIS));
}
static Error home_a(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    return home(bitnum_to_mask(A_AXIS));
}
static Error home_b(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    return home(bitnum_to_mask(B_AXIS));
}
static Error home_c(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    return home(bitnum_to_mask(C_AXIS));
}
static std::string limit_set(uint32_t mask) {
    const char* motor0AxisName = "xyzabc";
    std::string s;
    for (int axis = 0; axis < MAX_N_AXIS; axis++) {
        s += bitnum_is_true(mask, Machine::Axes::motor_bit(axis, 0)) ? char(motor0AxisName[axis]) : ' ';
    }
    const char* motor1AxisName = "XYZABC";
    for (int axis = 0; axis < MAX_N_AXIS; axis++) {
        s += bitnum_is_true(mask, Machine::Axes::motor_bit(axis, 1)) ? char(motor1AxisName[axis]) : ' ';
    }
    return s;
}
static Error show_limits(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    log_string(out, "Send ! to exit");
    log_stream(out, "Homing Axes : " << limit_set(Machine::Axes::homingMask));
    log_stream(out, "Limit Axes : " << limit_set(Machine::Axes::limitMask));
    log_string(out, "  PosLimitPins NegLimitPins Probe");

    const TickType_t interval = 500;
    TickType_t       limit    = xTaskGetTickCount();
    runLimitLoop              = true;
    do {
        TickType_t thisTime = xTaskGetTickCount();
        if (((long)(thisTime - limit)) > 0) {
            log_stream(out,
                       ": " << limit_set(Machine::Axes::posLimitMask) << " " << limit_set(Machine::Axes::negLimitMask)
                            << (config->_probe->get_state() ? " P" : ""));
            limit = thisTime + interval;
        }
        vTaskDelay(1);
        protocol_handle_events();
    } while (runLimitLoop);
    log_string(out, "");
    return Error::Ok;
}
static Error go_to_sleep(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    protocol_send_event(&sleepEvent);
    return Error::Ok;
}
static Error get_report_build_info(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (!value) {
        report_build_info(build_info->get(), out);
        return Error::Ok;
    }
    return Error::InvalidStatement;
}
static Error show_startup_lines(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    for (int i = 0; i < config->_macros->n_startup_lines; i++) {
        log_stream(out, "$N" << i << "=" << config->_macros->_startup_line[i]._gcode);
    }
    return Error::Ok;
}

std::map<const char*, uint8_t, cmp_str> restoreCommands = {
    { "$", SettingsRestore::Defaults },   { "settings", SettingsRestore::Defaults },
    { "#", SettingsRestore::Parameters }, { "gcode", SettingsRestore::Parameters },
    { "*", SettingsRestore::All },        { "all", SettingsRestore::All },
    { "@", SettingsRestore::Wifi },       { "wifi", SettingsRestore::Wifi },
};
static Error restore_settings(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (!value) {
        return Error::InvalidStatement;
    }
    auto it = restoreCommands.find(value);
    if (it == restoreCommands.end()) {
        return Error::InvalidStatement;
    }
    settings_restore(it->second);
    return Error::Ok;
}

static Error showState(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    const char* name;
    const State state = sys.state;
    auto        it    = StateName.find(state);
    name              = it == StateName.end() ? "<invalid>" : it->second;

    log_stream(out, "State " << int(state) << " (" << name << ")");
    return Error::Ok;
}

static Error doJog(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (sys.state == State::ConfigAlarm) {
        return Error::ConfigurationInvalid;
    }

    // For jogging, you must give gc_execute_line() a line that
    // begins with $J=.  There are several ways we can get here,
    // including  $J, $J=xxx, [J]xxx.  For any form other than
    // $J without =, we reconstruct a $J= line for gc_execute_line().
    if (!value) {
        return Error::InvalidStatement;
    }
    char jogLine[LINE_BUFFER_SIZE];
    strcpy(jogLine, "$J=");
    strcat(jogLine, value);
    return gc_execute_line(jogLine);
}

static Error listAlarms(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (sys.state == State::ConfigAlarm) {
        log_string(out, "Configuration alarm is active. Check the boot messages for 'ERR'.");
    } else if (sys.state == State::Alarm) {
        log_stream(out, "Active alarm: " << int(lastAlarm) << " (" << alarmString(lastAlarm));
    }
    if (value) {
        char*   endptr      = NULL;
        uint8_t alarmNumber = uint8_t(strtol(value, &endptr, 10));
        if (*endptr) {
            log_stream(out, "Malformed alarm number: " << value);
            return Error::InvalidValue;
        }
        const char* alarmName = alarmString(static_cast<ExecAlarm>(alarmNumber));
        if (alarmName) {
            log_stream(out, alarmNumber << ": " << alarmName);
            return Error::Ok;
        } else {
            log_stream(out, "Unknown alarm number: " << alarmNumber);
            return Error::InvalidValue;
        }
    }

    for (auto it = AlarmNames.begin(); it != AlarmNames.end(); it++) {
        log_stream(out, static_cast<int>(it->first) << ": " << it->second);
    }
    return Error::Ok;
}

const char* errorString(Error errorNumber) {
    auto it = ErrorNames.find(errorNumber);
    return it == ErrorNames.end() ? NULL : it->second;
}

static Error listErrors(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (value) {
        char* endptr      = NULL;
        int   errorNumber = strtol(value, &endptr, 10);
        if (*endptr) {
            log_stream(out, "Malformed error number: " << value);
            return Error::InvalidValue;
        }
        const char* errorName = errorString(static_cast<Error>(errorNumber));
        if (errorName) {
            log_stream(out, errorNumber << ": " << errorName);
            return Error::Ok;
        } else {
            log_stream(out, "Unknown error number: " << errorNumber);
            return Error::InvalidValue;
        }
    }

    for (auto it = ErrorNames.begin(); it != ErrorNames.end(); it++) {
        log_stream(out, static_cast<int>(it->first) << ": " << it->second);
    }
    return Error::Ok;
}

static Error motor_control(const char* value, bool disable) {
    if (sys.state == State::ConfigAlarm) {
        return Error::ConfigurationInvalid;
    }

    while (value && isspace(*value)) {
        ++value;
    }
    if (!value || *value == '\0') {
        log_info((disable ? "Dis" : "En") << "abling all motors");
        config->_axes->set_disable(disable);
        return Error::Ok;
    }

    auto axes = config->_axes;

    if (axes->_sharedStepperDisable.defined()) {
        log_error("Cannot " << (disable ? "dis" : "en") << "able individual axes with a shared disable pin");
        return Error::InvalidStatement;
    }

    for (int i = 0; i < config->_axes->_numberAxis; i++) {
        char axisName = axes->axisName(i);

        if (strchr(value, axisName) || strchr(value, tolower(axisName))) {
            log_info((disable ? "Dis" : "En") << "abling " << axisName << " motors");
            axes->set_disable(i, disable);
        }
    }
    return Error::Ok;
}
static Error motor_disable(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    return motor_control(value, true);
}

static Error motor_enable(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    return motor_control(value, false);
}

static Error motors_init(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    config->_axes->config_motors();
    return Error::Ok;
}

static Error macros_run(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (value) {
        log_info("Running macro" << *value);
        size_t macro_num = (*value) - '0';
        config->_macros->_macro[macro_num].run();
        return Error::Ok;
    }
    log_error("$Macros/Run requires a macro number argument");
    return Error::InvalidStatement;
}

static Error xmodem_receive(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (!value || !*value) {
        value = "uploaded";
    }
    FileStream* outfile;
    try {
        outfile = new FileStream(value, "w");
    } catch (...) {
        delay_ms(1000);   // Delay for FluidTerm to handle command echoing
        out.write(0x04);  // Cancel xmodem transfer with EOT
        log_info("Cannot open " << value);
        return Error::UploadFailed;
    }
    pollingPaused = true;
    bool oldCr    = out.setCr(false);
    delay_ms(1000);
    int size = xmodemReceive(&out, outfile);
    out.setCr(oldCr);
    pollingPaused = false;
    if (size >= 0) {
        log_info("Received " << size << " bytes to file " << outfile->path());
    } else {
        log_info("Reception failed or was canceled");
    }
    std::filesystem::path fname = outfile->fpath();
    delete outfile;
    HashFS::rehash_file(fname);

    return size < 0 ? Error::UploadFailed : Error::Ok;
}

static Error xmodem_send(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (!value || !*value) {
        value = "config.yaml";
    }
    FileStream* infile;
    try {
        infile = new FileStream(value, "r");
    } catch (...) {
        log_info("Cannot open " << value);
        return Error::DownloadFailed;
    }
    bool oldCr = out.setCr(false);
    log_info("Sending " << value << " via XModem");
    int size = xmodemTransmit(&out, infile);
    out.setCr(oldCr);
    delete infile;
    if (size >= 0) {
        log_info("Sent " << size << " bytes");
    } else {
        log_info("Sending failed or was canceled");
    }
    return size < 0 ? Error::DownloadFailed : Error::Ok;
}

static Error dump_config(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    Channel* ss;
    if (value) {
        // Use a file on the local file system unless there is an explicit prefix like /sd/
        std::error_code ec;

        try {
            //            ss = new FileStream(std::string(value), "", "w");
            ss = new FileStream(value, "w", "");
        } catch (Error err) { return err; }
    } else {
        ss = &out;
    }
    try {
        Configuration::Generator generator(*ss);
        config->group(generator);
    } catch (std::exception& ex) { log_info("Config dump error: " << ex.what()); }
    if (value) {
        drain_messages();
        delete ss;
    }
    return Error::Ok;
}

static Error fakeMaxSpindleSpeed(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (!value) {
        log_stream(out, "$30=" << spindle->maxSpeed());
    }
    return Error::Ok;
}

static Error fakeLaserMode(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (!value) {
        log_stream(out, "$32=" << (spindle->isRateAdjusted() ? "1" : "0"));
    }
    return Error::Ok;
}

static Error showChannelInfo(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    allChannels.listChannels(out);
    return Error::Ok;
}

static Error showStartupLog(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    StartupLog::dump(out);
    return Error::Ok;
}

static Error showGPIOs(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    gpio_dump(out);
    return Error::Ok;
}

static Error setReportInterval(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (!value) {
        uint32_t actual = out.getReportInterval();
        if (actual) {
            log_info_to(out, out.name() << " auto report interval is " << actual << " ms");
        } else {
            log_info_to(out, out.name() << " auto reporting is off");
        }
        return Error::Ok;
    }
    char*    endptr;
    uint32_t intValue = strtol(value, &endptr, 10);

    if (endptr == value || *endptr != '\0') {
        return Error::BadNumberFormat;
    }

    uint32_t actual = out.setReportInterval(intValue);
    if (actual) {
        log_info(out.name() << " auto report interval set to " << actual << " ms");
    } else {
        log_info(out.name() << " auto reporting turned off");
    }

    // Send a full status report immediately so the client has all the data
    report_wco_counter = 0;
    report_ovr_counter = 0;

    return Error::Ok;
}

static Error showHeap(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    log_info("Heap free: " << xPortGetFreeHeapSize() << " min: " << heapLowWater);
    return Error::Ok;
}

// Commands use the same syntax as Settings, but instead of setting or
// displaying a persistent value, a command causes some action to occur.
// That action could be anything, from displaying a run-time parameter
// to performing some system state change.  Each command is responsible
// for decoding its own value string, if it needs one.
void make_user_commands() {
    new UserCommand("GD", "GPIO/Dump", showGPIOs, anyState);

    new UserCommand("CI", "Channel/Info", showChannelInfo, anyState);
    new UserCommand("XR", "Xmodem/Receive", xmodem_receive, notIdleOrAlarm);
    new UserCommand("XS", "Xmodem/Send", xmodem_send, notIdleOrAlarm);
    new UserCommand("CD", "Config/Dump", dump_config, anyState);
    new UserCommand("", "Help", show_help, anyState);
    new UserCommand("T", "State", showState, anyState);
    new UserCommand("J", "Jog", doJog, notIdleOrJog);

    new UserCommand("$", "GrblSettings/List", report_normal_settings, cycleOrHold);
    new UserCommand("L", "GrblNames/List", list_grbl_names, cycleOrHold);
    new UserCommand("Limits", "Limits/Show", show_limits, cycleOrHold);
    new UserCommand("S", "Settings/List", list_settings, cycleOrHold);
    new UserCommand("SC", "Settings/ListChanged", list_changed_settings, cycleOrHold);
    new UserCommand("CMD", "Commands/List", list_commands, cycleOrHold);
    new UserCommand("A", "Alarms/List", listAlarms, anyState);
    new UserCommand("E", "Errors/List", listErrors, anyState);
    new UserCommand("G", "GCode/Modes", report_gcode, anyState);
    new UserCommand("C", "GCode/Check", toggle_check_mode, anyState);
    new UserCommand("X", "Alarm/Disable", disable_alarm_lock, anyState);
    new UserCommand("NVX", "Settings/Erase", Setting::eraseNVS, notIdleOrAlarm, WA);
    new UserCommand("V", "Settings/Stats", Setting::report_nvs_stats, notIdleOrAlarm);
    new UserCommand("#", "GCode/Offsets", report_ngc, notIdleOrAlarm);
    new UserCommand("H", "Home", home_all, notIdleOrAlarm);
    new UserCommand("MD", "Motor/Disable", motor_disable, notIdleOrAlarm);
    new UserCommand("ME", "Motor/Enable", motor_enable, notIdleOrAlarm);
    new UserCommand("MI", "Motors/Init", motors_init, notIdleOrAlarm);

    new UserCommand("RM", "Macros/Run", macros_run, notIdleOrAlarm);

    new UserCommand("HX", "Home/X", home_x, notIdleOrAlarm);
    new UserCommand("HY", "Home/Y", home_y, notIdleOrAlarm);
    new UserCommand("HZ", "Home/Z", home_z, notIdleOrAlarm);
    new UserCommand("HA", "Home/A", home_a, notIdleOrAlarm);
    new UserCommand("HB", "Home/B", home_b, notIdleOrAlarm);
    new UserCommand("HC", "Home/C", home_c, notIdleOrAlarm);

    new UserCommand("MU0", "Msg/Uart0", msg_to_uart0, anyState);
    new UserCommand("MU1", "Msg/Uart1", msg_to_uart1, anyState);
    new UserCommand("LM", "Log/Msg", cmd_log_msg, anyState);
    new UserCommand("LE", "Log/Error", cmd_log_error, anyState);
    new UserCommand("LW", "Log/Warn", cmd_log_warn, anyState);
    new UserCommand("LI", "Log/Info", cmd_log_info, anyState);
    new UserCommand("LD", "Log/Debug", cmd_log_debug, anyState);
    new UserCommand("LV  ", "Log/Verbose", cmd_log_verbose, anyState);

    new UserCommand("SLP", "System/Sleep", go_to_sleep, notIdleOrAlarm);
    new UserCommand("I", "Build/Info", get_report_build_info, notIdleOrAlarm);
    new UserCommand("N", "GCode/StartupLines", show_startup_lines, notIdleOrAlarm);
    new UserCommand("RST", "Settings/Restore", restore_settings, notIdleOrAlarm, WA);

    new UserCommand("Heap", "Heap/Show", showHeap, anyState);
    new UserCommand("SS", "Startup/Show", showStartupLog, anyState);

    new UserCommand("RI", "Report/Interval", setReportInterval, anyState);

    new UserCommand("30", "FakeMaxSpindleSpeed", fakeMaxSpindleSpeed, notIdleOrAlarm);
    new UserCommand("32", "FakeLaserMode", fakeLaserMode, notIdleOrAlarm);
};

// normalize_key puts a key string into canonical form -
// without whitespace.
// start points to a null-terminated string.
// Returns the first substring that does not contain whitespace.
// Case is unchanged because comparisons are case-insensitive.
char* normalize_key(char* start) {
    char c;

    // In the usual case, this loop will exit on the very first test,
    // because the first character is likely to be non-white.
    // Null ('\0') is not considered to be a space character.
    while (isspace(c = *start) && c != '\0') {
        ++start;
    }

    // start now points to either a printable character or end of string
    if (c == '\0') {
        return start;
    }

    // Having found the beginning of the printable string,
    // we now scan forward until we find a space character.
    char* end;
    for (end = start; (c = *end) != '\0' && !isspace(c); end++) {}

    // end now points to either a whitespace character or end of string
    // In either case it is okay to place a null there
    *end = '\0';

    return start;
}

// This is the handler for all forms of settings commands,
// $..= and [..], with and without a value.
Error do_command_or_setting(const char* key, const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    // If value is NULL, it means that there was no value string, i.e.
    // $key without =, or [key] with nothing following.
    // If value is not NULL, but the string is empty, that is the form
    // $key= with nothing following the = .  It is important to distinguish
    // those cases so that you can say "$N0=" to clear a startup line.

    // First search the yaml settings by name. If found, set a new
    // value if one is given, otherwise display the current value
    try {
        Configuration::RuntimeSetting rts(key, value, out);
        config->group(rts);

        if (rts.isHandled_) {
            if (value) {
                // Validate only if something changed, not for display
                try {
                    Configuration::Validator validator;
                    config->validate();
                    config->group(validator);
                } catch (std::exception& ex) {
                    log_error("Validation error: " << ex.what());
                    return Error::ConfigurationInvalid;
                }

                Configuration::AfterParse afterParseHandler;
                config->afterParse();
                config->group(afterParseHandler);
            }
            return Error::Ok;
        }
    } catch (const Configuration::ParseException& ex) {
        log_error("Configuration parse error at line " << ex.LineNumber() << ": " << ex.What());
        return Error::ConfigurationInvalid;
    } catch (const AssertionFailed& ex) {
        log_error("Configuration change failed: " << ex.what());
        return Error::ConfigurationInvalid;
    }

    // Next search the settings list by text name. If found, set a new
    // value if one is given, otherwise display the current value
    for (Setting* s : Setting::List) {
        if (strcasecmp(s->getName(), key) == 0) {
            if (auth_failed(s, value, auth_level)) {
                return Error::AuthenticationFailed;
            }
            if (value) {
                return s->setStringValue(uriDecode(value));
            } else {
                show_setting(s->getName(), s->getStringValue(), NULL, out);
                return Error::Ok;
            }
        }
    }

    // Then search the setting list by compatible name.  If found, set a new
    // value if one is given, otherwise display the current value in compatible mode
    for (Setting* s : Setting::List) {
        if (s->getGrblName() && strcasecmp(s->getGrblName(), key) == 0) {
            if (auth_failed(s, value, auth_level)) {
                return Error::AuthenticationFailed;
            }
            if (value) {
                return s->setStringValue(uriDecode(value));
            } else {
                show_setting(s->getGrblName(), s->getCompatibleValue(), NULL, out);
                return Error::Ok;
            }
        }
    }
    // If we did not find a setting, look for a command.  Commands
    // handle values internally; you cannot determine whether to set
    // or display solely based on the presence of a value.
    for (Command* cp : Command::List) {
        if ((strcasecmp(cp->getName(), key) == 0) || (cp->getGrblName() && strcasecmp(cp->getGrblName(), key) == 0)) {
            if (auth_failed(cp, value, auth_level)) {
                return Error::AuthenticationFailed;
            }
            return cp->action(value, auth_level, out);
        }
    }

    // If we did not find an exact match and there is no value,
    // indicating a display operation, we allow partial matches
    // and display every possibility.  This only applies to the
    // text form of the name, not to the nnn and ESPnnn forms.
    Error retval = Error::InvalidStatement;
    if (!value) {
        bool found = false;
        for (Setting* s : Setting::List) {
            auto test = s->getName();
            // The C++ standard regular expression library supports many more
            // regular expression forms than the simple one in Regex.cpp, but
            // consumes a lot of FLASH.  The extra capability is rarely useful
            // especially now that there are only a few NVS settings.
            if (regexMatch(key, test, false)) {
                const char* displayValue = auth_failed(s, value, auth_level) ? "<Authentication required>" : s->getStringValue();
                show_setting(test, displayValue, NULL, out);
                found = true;
            }
        }
        if (found) {
            return Error::Ok;
        }
    }
    return Error::InvalidStatement;
}

Error settings_execute_line(char* line, Channel& out, WebUI::AuthenticationLevel auth_level) {
    remove_password(line, auth_level);

    char* value;
    if (*line++ == '[') {  // [ESPxxx] form
        value = strchr(line, ']');
        if (!value) {
            // Missing ] is an error in this form
            return Error::InvalidStatement;
        }
        // ']' was found; replace it with null and set value to the rest of the line.
        *value++ = '\0';
        // If the rest of the line is empty, replace value with NULL.
        if (*value == '\0') {
            value = NULL;
        }
    } else {
        // $xxx form
        value = strchr(line, '=');
        if (value) {
            // $xxx=yyy form.
            *value++ = '\0';
        }
    }

    char* key = normalize_key(line);

    // At this point there are three possibilities for value
    // NULL - $xxx without =
    // NULL - [ESPxxx] with nothing after ]
    // empty string - $xxx= with nothing after
    // non-empty string - [ESPxxx]yyy or $xxx=yyy
    return do_command_or_setting(key, value, auth_level, out);
}

void settings_execute_startup() {
    if (sys.state != State::Idle) {
        return;
    }
    Error status_code;
    for (int i = 0; i < config->_macros->n_startup_lines; i++) {
        config->_macros->_startup_line[i].run();
    }
}

Error execute_line(char* line, Channel& channel, WebUI::AuthenticationLevel auth_level) {
    // Empty or comment line. For syncing purposes.
    if (line[0] == 0) {
        return Error::Ok;
    }
    // User '$' or WebUI '[ESPxxx]' command
    if (line[0] == '$' || line[0] == '[') {
        return settings_execute_line(line, channel, auth_level);
    }
    // Everything else is gcode. Block if in alarm or jog mode.
    if (sys.state == State::Alarm || sys.state == State::ConfigAlarm || sys.state == State::Jog) {
        return Error::SystemGcLock;
    }
    Error result = gc_execute_line(line);
    if (result != Error::Ok) {
        log_debug_to(channel, "Bad GCode: " << line);
    }
    return result;
}
