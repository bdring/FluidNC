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
#include "Uart.h"                 // Uart0.write()
#include "FileStream.h"           // FileStream()
#include "xmodem.h"               // xmodemReceive(), xmodemTransmit()

#include <cstring>
#include <map>

// WG Readable and writable as guest
// WU Readable and writable as user and admin
// WA Readable as user and admin, writable as admin

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

static void show_setting(const char* name, const char* value, const char* description, Channel& out) {
    out << "$" << name << "=" << value;
    if (description) {
        out << "    " << description;
    }
    out << '\n';
}

void settings_restore(uint8_t restore_flag) {
    if (restore_flag & SettingsRestore::Wifi) {
        WebUI::wifi_config.reset_settings();
    }

    if (restore_flag & SettingsRestore::Defaults) {
        bool restore_startup = restore_flag & SettingsRestore::StartupLines;
        for (Setting* s = Setting::List; s; s = s->next()) {
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
    for (Setting* s = Setting::List; s; s = s->next()) {
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
    out << "[HLP:$$ $+ $# $S $L $G $I $N $x=val $Nx=line $J=line $SLP $C $X $H $F $E=err ~ ! ? ctrl-x]\n";
    return Error::Ok;
}

static Error report_gcode(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    report_gcode_modes(out);
    return Error::Ok;
}

static void show_settings(Channel& out, type_t type) {
    for (Setting* s = Setting::List; s; s = s->next()) {
        if (s->getType() == type && s->getGrblName()) {
            // The following test could be expressed more succinctly with XOR,
            // but is arguably clearer when written out
            show_setting(s->getGrblName(), s->getCompatibleValue(), NULL, out);
        }
    }
}
static Error report_normal_settings(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    show_settings(out, GRBL);  // GRBL non-axis settings
    return Error::Ok;
}
static Error list_grbl_names(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    for (Setting* s = Setting::List; s; s = s->next()) {
        const char* gn = s->getGrblName();
        if (gn) {
            out << '$' << gn << " => $" << s->getName() << '\n';
        }
    }
    return Error::Ok;
}
static Error list_settings(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    for (Setting* s = Setting::List; s; s = s->next()) {
        const char* displayValue = auth_failed(s, value, auth_level) ? "<Authentication required>" : s->getStringValue();
        if (s->getType() != PIN) {
            show_setting(s->getName(), displayValue, NULL, out);
        }
    }
    return Error::Ok;
}
static Error list_changed_settings(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    for (Setting* s = Setting::List; s; s = s->next()) {
        const char* value = s->getStringValue();
        if (!auth_failed(s, value, auth_level) && strcmp(value, s->getDefaultString())) {
            if (s->getType() != PIN) {
                show_setting(s->getName(), value, NULL, out);
            }
        }
    }
    out << "(Passwords not shown)\n";
    return Error::Ok;
}
static Error list_commands(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    for (Command* cp = Command::List; cp; cp = cp->next()) {
        const char* name    = cp->getName();
        const char* oldName = cp->getGrblName();
        if (oldName) {
            out << '$' << name << " or $" << oldName;
        } else {
            out << '$' << name;
        }
        const char* description = cp->getDescription();
        if (description) {
            out << " =" << description;
        }
        out << '\n';
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
        log_debug("Check mode");
        mc_reset();
        report_feedback_message(Message::Disabled);
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
    if (config->_control->system_check_safety_door_ajar()) {
        rtAlarm = ExecAlarm::ControlPin;
        return Error::CheckDoor;
    }
    if (config->_control->stuck()) {
        log_info("Control pins:" << config->_control->report());
        rtAlarm = ExecAlarm::ControlPin;
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
        report_feedback_message(Message::AlarmUnlock);
        sys.state = State::Idle;
        // Don't run startup script. Prevents stored moves in startup from causing accidents.
    }  // Otherwise, no effect.
    return Error::Ok;
}
static Error report_ngc(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    report_ngc_parameters(out);
    return Error::Ok;
}
static Error home(int cycle) {
    if (sys.state == State::ConfigAlarm) {
        return Error::ConfigurationInvalid;
    }
    if (!Machine::Axes::homingMask) {
        return Error::SettingDisabled;
    }
    Error err = isStuck();
    if (err != Error::Ok) {
        return err;
    }
    sys.state = State::Homing;  // Set system state variable

    config->_stepping->beginLowLatency();

    mc_homing_cycle(cycle);

    config->_stepping->endLowLatency();

    if (!sys.abort) {             // Execute startup scripts after successful homing.
        sys.state = State::Idle;  // Set to IDLE when complete.
        Stepper::go_idle();       // Set steppers to the settings idle state before returning.
        if (cycle == Machine::Homing::AllCycles) {
            settings_execute_startup();
        }
    }
    return Error::Ok;
}
static Error home_all(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    return home(Machine::Homing::AllCycles);
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
static void write_limit_set(uint32_t mask, Channel& out) {
    const char* motor0AxisName = "xyzabc";
    for (int i = 0; i < MAX_N_AXIS; i++) {
        out.write(bitnum_is_true(mask, i) ? char(motor0AxisName[i]) : ' ');
    }
    const char* motor1AxisName = "XYZABC";
    for (int i = 0; i < MAX_N_AXIS; i++) {
        out.write(bitnum_is_true(mask, i + 16) ? char(motor1AxisName[i]) : ' ');
    }
}
static Error show_limits(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    out.print("Send ! to exit\n");
    out.print("Homing Axes: ");
    write_limit_set(Machine::Axes::homingMask, out);
    out.write('\n');
    out.print("Limit  Axes: ");
    write_limit_set(Machine::Axes::limitMask, out);
    out.write('\n');
    out.print("  PosLimitPins NegLimitPins\n");
    do {
        out.print(": ");  // Prevents WebUI from suppressing an empty line
        write_limit_set(Machine::Axes::posLimitMask, out);
        out.write(' ');
        write_limit_set(Machine::Axes::negLimitMask, out);
        out.print("\r\n");
        vTaskDelay(500);  // Delay for a reasonable repeat rate
        pollChannels();
    } while (!rtFeedHold);
    rtFeedHold = false;
    out.write('\n');
    return Error::Ok;
}
static Error go_to_sleep(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    rtSleep = true;
    return Error::Ok;
}
static Error get_report_build_info(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (!value) {
        report_build_info(build_info->get(), out);
        return Error::Ok;
    }
    return Error::InvalidStatement;
}
static Error report_startup_lines(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    for (int i = 0; i < config->_macros->n_startup_lines; i++) {
        out << "$N" << i << "=" << config->_macros->startup_line(i) << '\n';
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

    out << "State " << static_cast<int>(state) << " (" << name << ")\n";
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
    return gc_execute_line(jogLine, out);
}

static const char* alarmString(ExecAlarm alarmNumber) {
    auto it = AlarmNames.find(alarmNumber);
    return it == AlarmNames.end() ? NULL : it->second;
}

static Error listAlarms(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (sys.state == State::ConfigAlarm) {
        out << "Configuration alarm is active. Check the boot messages for 'ERR'.\n";
    } else if (rtAlarm != ExecAlarm::None) {
        out << "Active alarm: " << int(rtAlarm) << " (" << alarmString(rtAlarm) << ")\n";
    }
    if (value) {
        char*   endptr      = NULL;
        uint8_t alarmNumber = uint8_t(strtol(value, &endptr, 10));
        if (*endptr) {
            out << "Malformed alarm number: " << value << '\n';
            return Error::InvalidValue;
        }
        const char* alarmName = alarmString(static_cast<ExecAlarm>(alarmNumber));
        if (alarmName) {
            out << alarmNumber << ": " << alarmName << '\n';
            return Error::Ok;
        } else {
            out << "Unknown alarm number: " << alarmNumber << '\n';
            return Error::InvalidValue;
        }
    }

    for (auto it = AlarmNames.begin(); it != AlarmNames.end(); it++) {
        out << static_cast<int>(it->first) << ": " << it->second << '\n';
    }
    return Error::Ok;
}

const char* errorString(Error errorNumber) {
    auto it = ErrorNames.find(errorNumber);
    return it == ErrorNames.end() ? NULL : it->second;
}

static Error listErrors(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (value) {
        char*   endptr      = NULL;
        uint8_t errorNumber = uint8_t(strtol(value, &endptr, 10));
        if (*endptr) {
            out << "Malformed error number: " << value << '\n';
            return Error::InvalidValue;
        }
        const char* errorName = errorString(static_cast<Error>(errorNumber));
        if (errorName) {
            out << errorNumber << ": " << errorName << '\n';
            return Error::Ok;
        } else {
            out << "Unknown error number: " << errorNumber << '\n';
            return Error::InvalidValue;
        }
    }

    for (auto it = ErrorNames.begin(); it != ErrorNames.end(); it++) {
        out << static_cast<int>(it->first) << ": " << it->second << '\n';
    }
    return Error::Ok;
}

static Error motor_disable(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (sys.state == State::ConfigAlarm) {
        return Error::ConfigurationInvalid;
    }

    while (value && isspace(*value)) {
        ++value;
    }
    if (!value || *value == '\0') {
        log_info("Disabling all motors");
        config->_axes->set_disable(true);
        return Error::Ok;
    }

    auto axes = config->_axes;

    if (axes->_sharedStepperDisable.defined()) {
        log_error("Cannot disable individual axes with a shared disable pin");
        return Error::InvalidStatement;
    }

    for (int i = 0; i < config->_axes->_numberAxis; i++) {
        char axisName = axes->axisName(i);

        if (strchr(value, axisName) || strchr(value, tolower(axisName))) {
            log_info("Disabling " << String(axisName) << " motors");
            axes->set_disable(i, true);
        }
    }
    return Error::Ok;
}

static Error xmodem_receive(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (!value || !*value) {
        value = "uploaded";
    }
    FileStream* outfile;
    try {
        outfile = new FileStream(value, "w", "/localfs");
    } catch (...) {
        vTaskDelay(1000);   // Delay for FluidTerm to handle command echoing
        Uart0.write(0x04);  // Cancel xmodem transfer with EOT
        log_info("Cannot open " << value);
        return Error::UploadFailed;
    }
    int size = xmodemReceive(&Uart0, outfile);
    if (size >= 0) {
        log_info("Received " << size << " bytes to file " << outfile->path());
    } else {
        log_info("Reception failed or was canceled");
    }
    delete outfile;
    return size < 0 ? Error::UploadFailed : Error::Ok;
}

static Error xmodem_send(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (!value || !*value) {
        value = "config.yaml";
    }
    Channel* infile;
    try {
        infile = new FileStream(value, "r");
    } catch (...) {
        log_info("Cannot open " << value);
        return Error::DownloadFailed;
    }
    log_info("Sending " << value << " via XModem");
    int size = xmodemTransmit(&Uart0, infile);
    delete infile;
    if (size >= 0) {
        log_info("Sent " << size << " bytes");
    } else {
        log_info("Sending failed or was canceled");
    }
    return size < 0 ? Error::DownloadFailed : Error::Ok;
}

static Error dump_config(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    Print* ss;
    try {
        if (value) {
            // Use a file on the local file system unless there is an explicit prefix like /sd/
            ss = new FileStream(value, "w", "/localfs");
        } else {
            ss = &out;
        }
    } catch (Error err) { return err; }
    try {
        Configuration::Generator generator(*ss);
        config->group(generator);
    } catch (std::exception& ex) { log_info("Config dump error: " << ex.what()); }
    if (value) {
        delete ss;
    }
    return Error::Ok;
}

static Error fakeLaserMode(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (!value) {
        out << "$32=" << (spindle->isRateAdjusted() ? "1" : "0") << '\n';
    }
    return Error::Ok;
}

static Error showChannelInfo(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    out << allChannels.info();
    return Error::Ok;
}

// Commands use the same syntax as Settings, but instead of setting or
// displaying a persistent value, a command causes some action to occur.
// That action could be anything, from displaying a run-time parameter
// to performing some system state change.  Each command is responsible
// for decoding its own value string, if it needs one.
void make_user_commands() {
    new UserCommand("CI", "Channel/Info", showChannelInfo, anyState);
    new UserCommand("XR", "Xmodem/Receive", xmodem_receive, notIdleOrAlarm);
    new UserCommand("XS", "Xmodem/Send", xmodem_send, notIdleOrJog);
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

    new UserCommand("HX", "Home/X", home_x, notIdleOrAlarm);
    new UserCommand("HY", "Home/Y", home_y, notIdleOrAlarm);
    new UserCommand("HZ", "Home/Z", home_z, notIdleOrAlarm);
    new UserCommand("HA", "Home/A", home_a, notIdleOrAlarm);
    new UserCommand("HB", "Home/B", home_b, notIdleOrAlarm);
    new UserCommand("HC", "Home/C", home_c, notIdleOrAlarm);

    new UserCommand("SLP", "System/Sleep", go_to_sleep, notIdleOrAlarm);
    new UserCommand("I", "Build/Info", get_report_build_info, notIdleOrAlarm);
    new UserCommand("N", "GCode/StartupLines", report_startup_lines, notIdleOrAlarm);
    new UserCommand("RST", "Settings/Restore", restore_settings, notIdleOrAlarm, WA);

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
Error do_command_or_setting(const char* key, char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
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
        log_error("Configuration parse error: " << ex.What() << " @ " << ex.LineNumber() << ":" << ex.ColumnNumber());
        return Error::ConfigurationInvalid;
    } catch (const AssertionFailed& ex) {
        log_error("Configuration change failed: " << ex.what());
        return Error::ConfigurationInvalid;
    }

    // Next search the settings list by text name. If found, set a new
    // value if one is given, otherwise display the current value
    for (Setting* s = Setting::List; s; s = s->next()) {
        if (strcasecmp(s->getName(), key) == 0) {
            if (auth_failed(s, value, auth_level)) {
                return Error::AuthenticationFailed;
            }
            if (value) {
                return s->setStringValue(value);
            } else {
                show_setting(s->getName(), s->getStringValue(), NULL, out);
                return Error::Ok;
            }
        }
    }

    // Then search the setting list by compatible name.  If found, set a new
    // value if one is given, otherwise display the current value in compatible mode
    for (Setting* s = Setting::List; s; s = s->next()) {
        if (s->getGrblName() && strcasecmp(s->getGrblName(), key) == 0) {
            if (auth_failed(s, value, auth_level)) {
                return Error::AuthenticationFailed;
            }
            if (value) {
                return s->setStringValue(value);
            } else {
                show_setting(s->getGrblName(), s->getCompatibleValue(), NULL, out);
                return Error::Ok;
            }
        }
    }
    // If we did not find a setting, look for a command.  Commands
    // handle values internally; you cannot determine whether to set
    // or display solely based on the presence of a value.
    for (Command* cp = Command::List; cp; cp = cp->next()) {
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
        auto lcKey = String(key);
        lcKey.toLowerCase();
        bool found = false;
        for (Setting* s = Setting::List; s; s = s->next()) {
            auto lcTest = String(s->getName());
            lcTest.toLowerCase();

            if (regexMatch(lcKey.c_str(), lcTest.c_str())) {
                const char* displayValue = auth_failed(s, value, auth_level) ? "<Authentication required>" : s->getStringValue();
                show_setting(s->getName(), displayValue, NULL, out);
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
        value = strrchr(line, ']');
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
    Error status_code;
    for (int i = 0; i < config->_macros->n_startup_lines; i++) {
        String      str = config->_macros->startup_line(i);
        const char* s   = str.c_str();
        if (s && strlen(s)) {
            // We have to copy this to a mutable array because
            // gc_execute_line modifies the line while parsing.
            char gcline[256];
            strncpy(gcline, s, 255);
            status_code = gc_execute_line(gcline, Uart0);
            Uart0 << ">" << gcline << ":";
            report_status_message(status_code, Uart0);
        }
    }
}

Error execute_line(char* line, Channel& channel, WebUI::AuthenticationLevel auth_level) {
    Error result = Error::Ok;
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
    return gc_execute_line(line, channel);
}
