// Copyright (c) 2020 Mitch Bradley
// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  WebSettings.cpp - Settings and Commands for the interface
  to ESP3D_WebUI.  Code snippets extracted from commands.cpp in the
  old WebUI interface code are presented via the Settings class.
*/

#include "WebSettings.h"

#include "../Settings.h"
#include "../Machine/MachineConfig.h"
#include "../Configuration/JsonGenerator.h"
#include "../Uart.h"       // Uart0.baud
#include "../Report.h"     // git_info
#include "../InputFile.h"  // infile

#include "Driver/localfs.h"  // localfs_format

#include "Commands.h"  // COMMANDS::restart_MCU();
#include "WifiConfig.h"

#include <cstring>

namespace WebUI {

    static String LeftJustify(const char* s, size_t width) {
        String ret = s;

        for (size_t l = ret.length(); width > l; width--) {
            ret += ' ';
        }
        return ret;
    }

    typedef struct {
        char* key;
        char* value;
    } keyval_t;

    static keyval_t params[10];
    bool            split_params(char* parameter) {
        int i = 0;
        for (char* s = parameter; *s; s++) {
            if (*s == '=') {
                params[i].value = s + 1;
                *s              = '\0';
                // Search backward looking for the start of the key,
                // either just after a space or at the beginning of the strin
                if (s == parameter) {
                    return false;
                }
                for (char* k = s - 1; k >= parameter; --k) {
                    if (*k == '\0') {
                        // If we find a NUL - i.e. the end of the previous key -
                        // before finding a space, the string is malformed.
                        return false;
                    }
                    if (*k == ' ') {
                        *k              = '\0';
                        params[i++].key = k + 1;
                        break;
                    }
                    if (k == parameter) {
                        params[i++].key = k;
                    }
                }
            }
        }
        params[i].key = NULL;
        return true;
    }

    char  nullstr[1] = { '\0' };
    char* get_param(const char* key, bool allowSpaces) {
        for (keyval_t* p = params; p->key; p++) {
            if (!strcasecmp(key, p->key)) {
                if (!allowSpaces) {
                    for (char* s = p->value; *s; s++) {
                        if (*s == ' ') {
                            *s = '\0';
                            break;
                        }
                    }
                }
                return p->value;
            }
        }
        return nullstr;
    }
}

Error WebCommand::action(char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (_cmdChecker && _cmdChecker()) {
        return Error::AnotherInterfaceBusy;
    }
    char empty = '\0';
    if (!value) {
        value = &empty;
    }
    return _action(value, auth_level, out);
};

namespace WebUI {
    // Used by js/connectdlg.js
    static Error showFwInfo(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP800
        out << "FW version: FluidNC " << git_info;
        // TODO: change grbl-embedded to FluidNC after fixing WebUI
        out << " # FW target:grbl-embedded  # FW HW:";

        // std::error_code ec;
        // FluidPath { "/sd", ec };
        // out << (ec ? "No SD" : "Direct SD");

        // We do not check the SD presence here because if the SD card is out,
        // WebUI will switch to M20 for SD access, which is wrong for FluidNC
        out << "Direct SD";

        out << "  # primary sd:/sd # secondary sd:none # authentication:";
#ifdef ENABLE_AUTHENTICATION
        out << "yes";
#else
        out << "no";
#endif
        out << wifi_config.webInfo();

        //to save time in decoding `?`
        out << " # axis:" << String(config->_axes->_numberAxis) << '\n';
        return Error::Ok;
    }

    static Error LocalFSSize(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP720
        std::error_code ec;

        auto space = stdfs::space("/spiffs", ec);
        if (ec) {
            out << "Error " << ec.message() << '\n';
            return Error::FsFailedMount;
        }
        auto totalBytes = space.capacity;
        auto freeBytes  = space.available;
        auto usedBytes  = totalBytes - freeBytes;

        out << parameter << "LocalFS  Total:" << formatBytes(localfs_size());
        out << " Used:" << formatBytes(usedBytes) << '\n';
        return Error::Ok;
    }

    static Error formatLocalFS(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP710
        if (localfs_format(parameter)) {
            return Error::FsFailedFormat;
        }
        out << "Filesystem Formatted\n";
        return Error::Ok;
    }

#ifdef ENABLE_AUTHENTICATION
    static Error setUserPassword(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP555
        if (*parameter == '\0') {
            user_password->setDefault();
            return Error::Ok;
        }
        if (user_password->setStringValue(parameter) != Error::Ok) {
            out << "Invalid Password" << '\n';
            return Error::InvalidValue;
        }
        return Error::Ok;
    }
#endif

    static Error restart(char* parameter, AuthenticationLevel auth_level, Channel& out) {
        log_info("Restarting");
        COMMANDS::restart_MCU();
        return Error::Ok;
    }

    // used by js/restartdlg.js
    static Error setSystemMode(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP444
        parameter = trim(parameter);
        if (strcasecmp(parameter, "RESTART") != 0) {
            out << "Parameter must be RESTART" << '\n';
            return Error::InvalidValue;
        }
        return restart(parameter, auth_level, out);
    }

    // Used by js/statusdlg.js
    static Error showSysStats(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP420
        out << "Chip ID: " << (uint16_t)(ESP.getEfuseMac() >> 32) << '\n';
        out << "CPU Frequency: " << ESP.getCpuFreqMHz() + "Mhz" << '\n';
        out << "CPU Temperature: " << String(temperatureRead(), 1) << "C\n";
        out << "Free memory: " << formatBytes(ESP.getFreeHeap()) << '\n';
        out << "SDK: " << ESP.getSdkVersion() << '\n';
        out << "Flash Size: " << formatBytes(ESP.getFlashChipSize()) << '\n';

        // Round baudRate to nearest 100 because ESP32 can say e.g. 115201
        out << "Baud rate: " << String((Uart0.baud / 100) * 100) << '\n';

        WiFiConfig::showWifiStats(out);

        String info = bt_config.info();
        if (info.length()) {
            out << info << '\n';
        }
        out << "FW version: FluidNC " << git_info << '\n';
        return Error::Ok;
    }

    static Error setWebSetting(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP401
        // The string is of the form "P=name T=type V=value
        // We do not need the "T=" (type) parameter because the
        // Setting objects know their own type.  We do not use
        // split_params because if fails if the value string
        // contains '='
        if (strncmp(parameter, "P=", strlen("P="))) {
            return Error::InvalidValue;
        }
        char* spos = &parameter[2];
        char* scan;
        for (scan = spos; *scan != ' ' && *scan != '\0'; ++scan) {}
        if (*scan == '\0') {
            return Error::InvalidValue;
        }
        // *scan is ' ' so we have found the end of the spos string
        *scan++ = '\0';

        if (strncmp(scan, "T=", strlen("T="))) {
            return Error::InvalidValue;
        }
        // Find the end of the T=type string
        for (scan += strlen("T="); *scan != ' ' && *scan != '\0'; ++scan) {}
        if (strncmp(scan, " V=", strlen(" V="))) {
            return Error::InvalidValue;
        }
        char* sval = scan + strlen(" V=");

        Error ret = do_command_or_setting(spos, sval, auth_level, out);
        return ret;
    }

    // Used by js/setting.js
    static Error listSettings(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP400
        JSONencoder j(true, out);
        j.begin();
        j.begin_array("EEPROM");

        // NVS settings
        j.setCategory("nvs");
        for (Setting* js = Setting::List; js; js = js->next()) {
            js->addWebui(&j);
        }

        // Configuration tree
        j.setCategory("tree");
        Configuration::JsonGenerator gen(j);
        config->group(gen);

        j.end_array();
        j.end();
        out << '\n';

        return Error::Ok;
    }

    static Error openFile(const char* fs, char* parameter, AuthenticationLevel auth_level, Channel& out) {
        if (*parameter == '\0') {
            out << "Missing file name!" << '\n';
            return Error::InvalidValue;
        }
        String path = trim(parameter);
        if (path[0] != '/') {
            path = "/" + path;
        }

        try {
            infile = new InputFile(fs, path.c_str(), auth_level, out);
        } catch (Error err) { return err; }
        return Error::Ok;
    }

    static Error showFile(const char* fs, char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP221
        if (notIdleOrAlarm()) {
            return Error::IdleError;
        }
        Error err;
        if ((err = openFile(fs, parameter, auth_level, out)) != Error::Ok) {
            return err;
        }
        char  fileLine[255];
        Error res;
        while ((res = infile->readLine(fileLine, 255)) == Error::Ok) {
            out << fileLine << '\n';
        }
        if (res != Error::Eof) {
            out << errorString(res) << '\n';
        }
        delete infile;
        infile = nullptr;
        return Error::Ok;
    }

    static Error showSDFile(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP221
        return showFile("/sd", parameter, auth_level, out);
    }
    static Error showLocalFile(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP701
        return showFile("", parameter, auth_level, out);
    }

    static Error runFile(const char* fs, char* parameter, AuthenticationLevel auth_level, Channel& out) {
        Error err;
        if (sys.state == State::Alarm || sys.state == State::ConfigAlarm) {
            out << "Alarm" << '\n';
            return Error::IdleError;
        }
        if (sys.state != State::Idle) {
            out << "Busy" << '\n';
            return Error::IdleError;
        }
        if ((err = openFile(fs, parameter, auth_level, out)) != Error::Ok) {
            return err;
        }
        readyNext = true;
        report_realtime_status(out);
        return Error::Ok;
    }

    static Error runSDFile(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP220
        return runFile("/sd", parameter, auth_level, out);
    }

    // Used by js/controls.js
    static Error runLocalFile(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP700
        return runFile("", parameter, auth_level, out);
    }

    static Error deleteObject(const char* fs, char* name, Channel& out) {
        std::error_code ec;

        FluidPath fpath { name, fs, ec };
        if (ec) {
            out << "No SD card\n";
            return Error::FsFailedMount;
        }
        auto isDir = stdfs::is_directory(fpath, ec);
        if (ec) {
            out << "Delete failed: " << ec.message() << '\n';
            return Error::FsFileNotFound;
        }
        if (isDir) {
            stdfs::remove_all(fpath, ec);
            if (ec) {
                out << "Delete Directory failed: " << ec.message() << '\n';
                return Error::FsFailedDelDir;
            }
        } else {
            stdfs::remove(fpath, ec);
            if (ec) {
                out << "Delete File failed: " << ec.message() << '\n';
                return Error::FsFailedDelFile;
            }
        }

        return Error::Ok;
    }

    static Error deleteSDObject(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP215
        return deleteObject("/sd", parameter, out);
    }

    static Error deleteLocalFile(char* parameter, AuthenticationLevel auth_level, Channel& out) {
        return deleteObject("/spiffs", parameter, out);
    }

    static Error listFilesystem(const char* fs, const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
        std::error_code ec;

        FluidPath fpath { value, fs, ec };
        if (ec) {
            out << "No SD card\n";
            return Error::FsFailedMount;
        }

        auto iter = stdfs::recursive_directory_iterator { fpath, ec };
        if (ec) {
            out << "Error: " << ec.message() << '\n';
            return Error::FsFailedMount;
        }
        for (auto const& dir_entry : iter) {
            if (dir_entry.is_directory()) {
                out << "[DIR:" << std::string(iter.depth(), ' ').c_str() << dir_entry.path().filename() << "]\n";
            } else {
                out << "[FILE: " << std::string(iter.depth(), ' ').c_str() << dir_entry.path().filename()
                    << "|SIZE:" << dir_entry.file_size() << "]\n";
            }
        }
        auto space = stdfs::space(fpath, ec);
        if (ec) {
            out << "Error " << ec.value() << " " << ec.message() << '\n';
            return Error::FsFailedMount;
        }

        auto totalBytes = space.capacity;
        auto freeBytes  = space.available;
        auto usedBytes  = totalBytes - freeBytes;
        out << "[" << fpath.c_str();
        out << " Free:" << formatBytes(freeBytes);
        out << " Used:" << formatBytes(usedBytes);
        out << " Total:" << formatBytes(totalBytes);
        out << "]\n";
        return Error::Ok;
    }

    static Error listSDFiles(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP210
        return listFilesystem("/sd", parameter, auth_level, out);
    }

    static Error listLocalFiles(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // No ESP command
        return listFilesystem("/spiffs", parameter, auth_level, out);
    }

    // Used by js/files.js
    static Error showSDStatus(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP200
        std::error_code ec;

        FluidPath path { "", "/sd", ec };
        if (ec) {
            out << "No SD card\n";
            return Error::FsFailedMount;
        }
        out << "SD card detected\n";
        return Error::Ok;
    }

    static Error setRadioState(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP115
        parameter = trim(parameter);
        if (*parameter == '\0') {
            // Display the radio state
            bool on = wifi_config.isOn() || bt_config.isOn();
            out << (on ? "ON" : "OFF") << '\n';
            return Error::Ok;
        }
        int8_t on = -1;
        if (strcasecmp(parameter, "ON") == 0) {
            on = 1;
        } else if (strcasecmp(parameter, "OFF") == 0) {
            on = 0;
        }
        if (on == -1) {
            out << "only ON or OFF mode supported!" << '\n';
            return Error::InvalidValue;
        }

        //Stop everything
        wifi_config.end();
        bt_config.end();

        //if On start proper service
        if (on && (wifi_config.begin() || bt_config.begin())) {
            return Error::Ok;
        }
        out << "[MSG: Radio is Off]" << '\n';
        return Error::Ok;
    }

    static Error showWebHelp(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP0
        out << "Persistent web settings - $name to show, $name=value to set" << '\n';
        out << "ESPname FullName         Description" << '\n';
        out << "------- --------         -----------" << '\n';
        for (Setting* s = Setting::List; s; s = s->next()) {
            if (s->getType() == WEBSET) {
                out << LeftJustify(s->getGrblName() ? s->getGrblName() : "", 8);
                out << LeftJustify(s->getName(), 25 - 8);
                out << s->getDescription() << '\n';
            }
        }
        out << '\n';
        out << "Other web commands: $name to show, $name=value to set" << '\n';
        out << "ESPname FullName         Values" << '\n';
        out << "------- --------         ------" << '\n';
        for (Command* cp = Command::List; cp; cp = cp->next()) {
            if (cp->getType() == WEBCMD) {
                out << LeftJustify(cp->getGrblName() ? cp->getGrblName() : "", 8);
                out << LeftJustify(cp->getName(), 25 - 8);
                if (cp->getDescription()) {
                    out << cp->getDescription();
                }
                out << '\n';
            }
        }
        return Error::Ok;
    }

    void make_authentication_settings() {
#ifdef ENABLE_AUTHENTICATION
        new WebCommand("password", WEBCMD, WA, "ESP555", "WebUI/SetUserPassword", setUserPassword);
        user_password  = new StringSetting("User password",
                                          WEBSET,
                                          WA,
                                          NULL,
                                          "WebUI/UserPassword",
                                          DEFAULT_USER_PWD,
                                          MIN_LOCAL_PASSWORD_LENGTH,
                                          MAX_LOCAL_PASSWORD_LENGTH,
                                          &COMMANDS::isLocalPasswordValid);
        admin_password = new StringSetting("Admin password",
                                           WEBSET,
                                           WA,
                                           NULL,
                                           "WebUI/AdminPassword",
                                           DEFAULT_ADMIN_PWD,
                                           MIN_LOCAL_PASSWORD_LENGTH,
                                           MAX_LOCAL_PASSWORD_LENGTH,
                                           &COMMANDS::isLocalPasswordValid);
#endif
    }

    void make_web_settings() {
        make_authentication_settings();
        // If authentication enabled, display_settings skips or displays <Authentication Required>
        // RU - need user or admin password to read
        // WU - need user or admin password to set
        // WA - need admin password to set
        new WebCommand(NULL, WEBCMD, WG, "ESP800", "Firmware/Info", showFwInfo, anyState);
        new WebCommand(NULL, WEBCMD, WU, "ESP420", "System/Stats", showSysStats, anyState);
        new WebCommand("RESTART", WEBCMD, WA, "ESP444", "System/Control", setSystemMode);
        new WebCommand("RESTART", WEBCMD, WA, NULL, "Bye", restart);

        new WebCommand(NULL, WEBCMD, WU, "ESP720", "LocalFS/Size", LocalFSSize);
        new WebCommand("FORMAT", WEBCMD, WA, "ESP710", "LocalFS/Format", formatLocalFS);
        new WebCommand("path", WEBCMD, WU, "ESP701", "LocalFS/Show", showLocalFile);
        new WebCommand("path", WEBCMD, WU, "ESP700", "LocalFS/Run", runLocalFile);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/List", listLocalFiles);
#if 0
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/ListJSON", listLocalFilesJSON);
#endif
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/Delete", deleteLocalFile);

        new WebCommand("path", WEBCMD, WU, "ESP221", "SD/Show", showSDFile);
        new WebCommand("path", WEBCMD, WU, "ESP220", "SD/Run", runSDFile);
        new WebCommand("file_or_directory_path", WEBCMD, WU, "ESP215", "SD/Delete", deleteSDObject);
        new WebCommand(NULL, WEBCMD, WU, "ESP210", "SD/List", listSDFiles);
        new WebCommand(NULL, WEBCMD, WU, "ESP200", "SD/Status", showSDStatus);

        new WebCommand("ON|OFF", WEBCMD, WA, "ESP115", "Radio/State", setRadioState);

        new WebCommand("P=position T=type V=value", WEBCMD, WA, "ESP401", "WebUI/Set", setWebSetting);
        new WebCommand(NULL, WEBCMD, WU, "ESP400", "WebUI/List", listSettings, anyState);
        new WebCommand(NULL, WEBCMD, WG, "ESP0", "WebUI/Help", showWebHelp, anyState);
        new WebCommand(NULL, WEBCMD, WG, "ESP", "WebUI/Help", showWebHelp, anyState);
    }
}
