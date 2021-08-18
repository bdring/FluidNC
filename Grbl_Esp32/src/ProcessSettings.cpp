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

static void show_setting(const char* name, const char* value, const char* description, WebUI::ESPResponseStream* out) {
    grbl_sendf(out->client(), "$%s=%s", name, value);
    if (description) {
        grbl_sendf(out->client(), "    %s", description);
    }
    grbl_sendf(out->client(), "\r\n");
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
extern void make_grbl_commands();

namespace WebUI {
    extern void make_web_settings();
}

void settings_init() {
    make_settings();
    WebUI::make_web_settings();
    load_settings();
}

static Error show_grbl_help(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    report_grbl_help(out->client());
    return Error::Ok;
}

static Error report_gcode(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    report_gcode_modes(out->client());
    return Error::Ok;
}

static void show_grbl_settings(WebUI::ESPResponseStream* out, type_t type, bool wantAxis) {
    for (Setting* s = Setting::List; s; s = s->next()) {
        if (s->getType() == type && s->getGrblName()) {
            bool isAxis = s->getAxis() != NO_AXIS;
            // The following test could be expressed more succinctly with XOR,
            // but is arguably clearer when written out
            if ((wantAxis && isAxis) || (!wantAxis && !isAxis)) {
                show_setting(s->getGrblName(), s->getCompatibleValue(), NULL, out);
            }
        }
    }
}
static Error report_normal_settings(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    show_grbl_settings(out, GRBL, false);  // GRBL non-axis settings
    show_grbl_settings(out, GRBL, true);   // GRBL axis settings
    return Error::Ok;
}
static Error report_extended_settings(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    show_grbl_settings(out, GRBL, false);      // GRBL non-axis settings
    show_grbl_settings(out, EXTENDED, false);  // Extended non-axis settings
    show_grbl_settings(out, GRBL, true);       // GRBL axis settings
    show_grbl_settings(out, EXTENDED, true);   // Extended axis settings
    return Error::Ok;
}
static Error list_grbl_names(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    for (Setting* s = Setting::List; s; s = s->next()) {
        const char* gn = s->getGrblName();
        if (gn) {
            grbl_sendf(out->client(), "$%s => $%s\r\n", gn, s->getName());
        }
    }
    return Error::Ok;
}
static Error list_settings(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    for (Setting* s = Setting::List; s; s = s->next()) {
        const char* displayValue = auth_failed(s, value, auth_level) ? "<Authentication required>" : s->getStringValue();
        if (s->getType() != PIN) {
            show_setting(s->getName(), displayValue, NULL, out);
        }
    }
    return Error::Ok;
}
static Error list_changed_settings(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    for (Setting* s = Setting::List; s; s = s->next()) {
        const char* value = s->getStringValue();
        if (!auth_failed(s, value, auth_level) && strcmp(value, s->getDefaultString())) {
            if (s->getType() != PIN) {
                show_setting(s->getName(), value, NULL, out);
            }
        }
    }
    grbl_sendf(out->client(), "(Passwords not shown)\r\n");
    return Error::Ok;
}
static Error list_pins(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    for (Setting* s = Setting::List; s; s = s->next()) {
        const char* displayValue = auth_failed(s, value, auth_level) ? "<Authentication required>" : s->getStringValue();
        if (s->getType() == PIN) {
            show_setting(s->getName(), displayValue, NULL, out);
        }
    }
    return Error::Ok;
}
static Error list_changed_pins(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    for (Setting* s = Setting::List; s; s = s->next()) {
        const char* value = s->getStringValue();
        if (s->getType() == PIN && strcmp(value, "")) {  // Not undefined pin
            show_setting(s->getName(), value, NULL, out);
        }
    }
    return Error::Ok;
}
static Error list_commands(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    for (Command* cp = Command::List; cp; cp = cp->next()) {
        const char* name    = cp->getName();
        const char* oldName = cp->getGrblName();
        if (oldName) {
            grbl_sendf(out->client(), "$%s or $%s", name, oldName);
        } else {
            grbl_sendf(out->client(), "$%s", name);
        }
        const char* description = cp->getDescription();
        if (description) {
            grbl_sendf(out->client(), " =%s", description);
        }
        grbl_sendf(out->client(), "\r\n");
    }
    return Error::Ok;
}
static Error toggle_check_mode(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    if (sys.state == State::ConfigAlarm) {
        return Error::ConfigurationInvalid;
    }

    // Perform reset when toggling off. Check g-code mode should only work if Grbl
    // is idle and ready, regardless of alarm locks. This is mainly to keep things
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
static Error disable_alarm_lock(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    if (sys.state == State::ConfigAlarm) {
        return Error::ConfigurationInvalid;
    } else if (sys.state == State::Alarm) {
        // Block if safety door is ajar.
        if (config->_control->system_check_safety_door_ajar()) {
            return Error::CheckDoor;
        }
        report_feedback_message(Message::AlarmUnlock);
        sys.state = State::Idle;
        // Don't run startup script. Prevents stored moves in startup from causing accidents.
    }  // Otherwise, no effect.
    return Error::Ok;
}
static Error report_ngc(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    report_ngc_parameters(out->client());
    return Error::Ok;
}
static Error home(int cycle) {
    if (sys.state == State::ConfigAlarm) {
        return Error::ConfigurationInvalid;
    }

    if (!Machine::Axes::homingMask) {
        return Error::SettingDisabled;
    }
    if (config->_control->system_check_safety_door_ajar()) {
        return Error::CheckDoor;  // Block if safety door is ajar.
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
static Error home_all(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    return home(Machine::Homing::AllCycles);
}
static Error home_x(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    return home(bitnum_to_mask(X_AXIS));
}
static Error home_y(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    return home(bitnum_to_mask(Y_AXIS));
}
static Error home_z(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    return home(bitnum_to_mask(Z_AXIS));
}
static Error home_a(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    return home(bitnum_to_mask(A_AXIS));
}
static Error home_b(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    return home(bitnum_to_mask(B_AXIS));
}
static Error home_c(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    return home(bitnum_to_mask(C_AXIS));
}
static void write_limit_set(uint32_t mask) {
    const char* motor0AxisName = "xyzabc";
    for (int i = 0; i < MAX_N_AXIS; i++) {
        Uart0.write(bitnum_is_true(mask, i) ? uint8_t(motor0AxisName[i]) : ' ');
    }
    const char* motor1AxisName = "XYZABC";
    for (int i = 0; i < MAX_N_AXIS; i++) {
        Uart0.write(bitnum_is_true(mask, i + 16) ? uint8_t(motor1AxisName[i]) : ' ');
    }
}
static Error show_limits(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    Uart0.write("Send ! to exit\n");
    Uart0.write("Homing Axes: ");
    write_limit_set(Machine::Axes::homingMask);
    Uart0.write('\n');
    Uart0.write("Limit  Axes: ");
    write_limit_set(Machine::Axes::limitMask);
    Uart0.write('\n');
    Uart0.write("PosLimitPins NegLimitPins\n");
    do {
        write_limit_set(Machine::Axes::posLimitMask);
        Uart0.write(' ');
        write_limit_set(Machine::Axes::negLimitMask);
        Uart0.write('\n');
        vTaskDelay(500);
    } while (!rtFeedHold);
    rtFeedHold = false;
    Uart0.write('\n');
    return Error::Ok;
}
static Error sleep_grbl(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    rtSleep = true;
    return Error::Ok;
}
static Error get_report_build_info(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    if (!value) {
        report_build_info(build_info->get(), out->client());
        return Error::Ok;
    }
    return Error::InvalidStatement;
}
static Error report_startup_lines(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    for (int i = 0; i < config->_macros->n_startup_lines; i++) {
        report_startup_line(i, config->_macros->startup_line(i).c_str(), out->client());
    }
    return Error::Ok;
}

std::map<const char*, uint8_t, cmp_str> restoreCommands = {
    { "$", SettingsRestore::Defaults },   { "settings", SettingsRestore::Defaults },
    { "#", SettingsRestore::Parameters }, { "gcode", SettingsRestore::Parameters },
    { "*", SettingsRestore::All },        { "all", SettingsRestore::All },
    { "@", SettingsRestore::Wifi },       { "wifi", SettingsRestore::Wifi },
};
static Error restore_settings(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
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

static Error showState(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    const char* name;
    const State state = sys.state;
    auto        it    = StateName.find(state);
    name              = it == StateName.end() ? "<invalid>" : it->second;

    grbl_sendf(out->client(), "State %d (%s)\r\n", state, name);
    return Error::Ok;
}

static Error doJog(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
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
    return gc_execute_line(jogLine, out->client());
}

static const char* alarmString(ExecAlarm alarmNumber) {
    auto it = AlarmNames.find(alarmNumber);
    return it == AlarmNames.end() ? NULL : it->second;
}

static Error listAlarms(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    if (sys.state == State::ConfigAlarm) {
        grbl_sendf(out->client(), "Configuration alarm is active. Check the boot messages for 'ERR'.\r\n");
    } else if (rtAlarm != ExecAlarm::None) {
        grbl_sendf(out->client(), "Active alarm: %d (%s)\r\n", int(rtAlarm), alarmString(rtAlarm));
    }
    if (value) {
        char*   endptr      = NULL;
        uint8_t alarmNumber = uint8_t(strtol(value, &endptr, 10));
        if (*endptr) {
            grbl_sendf(out->client(), "Malformed alarm number: %s\r\n", value);
            return Error::InvalidValue;
        }
        const char* alarmName = alarmString(static_cast<ExecAlarm>(alarmNumber));
        if (alarmName) {
            grbl_sendf(out->client(), "%d: %s\r\n", alarmNumber, alarmName);
            return Error::Ok;
        } else {
            grbl_sendf(out->client(), "Unknown alarm number: %d\r\n", alarmNumber);
            return Error::InvalidValue;
        }
    }

    for (auto it = AlarmNames.begin(); it != AlarmNames.end(); it++) {
        grbl_sendf(out->client(), "%d: %s\r\n", it->first, it->second);
    }
    return Error::Ok;
}

const char* errorString(Error errorNumber) {
    auto it = ErrorNames.find(errorNumber);
    return it == ErrorNames.end() ? NULL : it->second;
}

static Error listErrors(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    if (value) {
        char*   endptr      = NULL;
        uint8_t errorNumber = uint8_t(strtol(value, &endptr, 10));
        if (*endptr) {
            grbl_sendf(out->client(), "Malformed error number: %s\r\n", value);
            return Error::InvalidValue;
        }
        const char* errorName = errorString(static_cast<Error>(errorNumber));
        if (errorName) {
            grbl_sendf(out->client(), "%d: %s\r\n", errorNumber, errorName);
            return Error::Ok;
        } else {
            grbl_sendf(out->client(), "Unknown error number: %d\r\n", errorNumber);
            return Error::InvalidValue;
        }
    }

    for (auto it = ErrorNames.begin(); it != ErrorNames.end(); it++) {
        grbl_sendf(out->client(), "%d: %s\r\n", it->first, it->second);
    }
    return Error::Ok;
}

static Error motor_disable(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
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

static Error dump_config(const char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
    ClientStream* ss;
    try {
        if (value) {
            // Use a file on the local file system unless there is an explicit prefix like /sd/
            ss = new ClientStream(value, "localfs");
        } else {
            ss = new ClientStream(CLIENT_ALL);
        }
    } catch (Error err) { return err; }
    try {
        Configuration::Generator generator(*ss);
        if (!value) {
            grbl_send(CLIENT_ALL, dataBeginMarker);
        }
        config->group(generator);
        if (!value) {
            grbl_send(out->client(), dataEndMarker);
        }
    } catch (std::exception& ex) { log_info("Config dump error: " << ex.what()); }
    if (ss) {
        delete ss;
    }
    return Error::Ok;
}

// Commands use the same syntax as Settings, but instead of setting or
// displaying a persistent value, a command causes some action to occur.
// That action could be anything, from displaying a run-time parameter
// to performing some system state change.  Each command is responsible
// for decoding its own value string, if it needs one.
void make_grbl_commands() {
    new GrblCommand("CD", "Config/Dump", dump_config, anyState);
    new GrblCommand("", "Help", show_grbl_help, anyState);
    new GrblCommand("T", "State", showState, anyState);
    new GrblCommand("J", "Jog", doJog, idleOrJog);

    new GrblCommand("$", "GrblSettings/List", report_normal_settings, notCycleOrHold);
    new GrblCommand("+", "ExtendedSettings/List", report_extended_settings, notCycleOrHold);
    new GrblCommand("L", "GrblNames/List", list_grbl_names, notCycleOrHold);
    new GrblCommand("Limits", "Limits/Show", show_limits, notCycleOrHold);
    new GrblCommand("S", "Settings/List", list_settings, notCycleOrHold);
    new GrblCommand("SC", "Settings/ListChanged", list_changed_settings, notCycleOrHold);
    new GrblCommand("P", "Pins/List", list_pins, notCycleOrHold);
    new GrblCommand("PC", "Pins/ListChanged", list_changed_pins, notCycleOrHold);
    new GrblCommand("CMD", "Commands/List", list_commands, notCycleOrHold);
    new GrblCommand("A", "Alarms/List", listAlarms, anyState);
    new GrblCommand("E", "Errors/List", listErrors, anyState);
    new GrblCommand("G", "GCode/Modes", report_gcode, anyState);
    new GrblCommand("C", "GCode/Check", toggle_check_mode, anyState);
    new GrblCommand("X", "Alarm/Disable", disable_alarm_lock, anyState);
    new GrblCommand("NVX", "Settings/Erase", Setting::eraseNVS, idleOrAlarm, WA);
    new GrblCommand("V", "Settings/Stats", Setting::report_nvs_stats, idleOrAlarm);
    new GrblCommand("#", "GCode/Offsets", report_ngc, idleOrAlarm);
    new GrblCommand("H", "Home", home_all, idleOrAlarm);
    new GrblCommand("MD", "Motor/Disable", motor_disable, idleOrAlarm);

    new GrblCommand("HX", "Home/X", home_x, idleOrAlarm);
    new GrblCommand("HY", "Home/Y", home_y, idleOrAlarm);
    new GrblCommand("HZ", "Home/Z", home_z, idleOrAlarm);
    new GrblCommand("HA", "Home/A", home_a, idleOrAlarm);
    new GrblCommand("HB", "Home/B", home_b, idleOrAlarm);
    new GrblCommand("HC", "Home/C", home_c, idleOrAlarm);

    new GrblCommand("SLP", "System/Sleep", sleep_grbl, idleOrAlarm);
    new GrblCommand("I", "Build/Info", get_report_build_info, idleOrAlarm);
    new GrblCommand("N", "GCode/StartupLines", report_startup_lines, idleOrAlarm);
    new GrblCommand("RST", "Settings/Restore", restore_settings, idleOrAlarm, WA);
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

    // end now points to either a whitespace character of end of string
    // In either case it is okay to place a null there
    *end = '\0';

    return start;
}

// This is the handler for all forms of settings commands,
// $..= and [..], with and without a value.
Error do_command_or_setting(const char* key, char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream* out) {
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

Error settings_execute_line(char* line, WebUI::ESPResponseStream* out, WebUI::AuthenticationLevel auth_level) {
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

Error settings_execute_line(char* line, uint8_t client, WebUI::AuthenticationLevel auth_level) {
    WebUI::ESPResponseStream stream(client, true);
    return settings_execute_line(line, &stream, auth_level);
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
            status_code = gc_execute_line(gcline, CLIENT_SERIAL);
            report_execute_startup_message(gcline, status_code, CLIENT_SERIAL);
        }
    }
}

Error execute_line(char* line, uint8_t client, WebUI::AuthenticationLevel auth_level) {
    Error result = Error::Ok;
    // Empty or comment line. For syncing purposes.
    if (line[0] == 0) {
        return Error::Ok;
    }
    // Grbl '$' or WebUI '[ESPxxx]' system command
    if (line[0] == '$' || line[0] == '[') {
        return settings_execute_line(line, client, auth_level);
    }
    // Everything else is gcode. Block if in alarm or jog mode.
    if (sys.state == State::Alarm || sys.state == State::ConfigAlarm || sys.state == State::Jog) {
        return Error::SystemGcLock;
    }
    return gc_execute_line(line, client);
}
