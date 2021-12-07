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

#include "Commands.h"  // COMMANDS::restart_MCU();
#include "WifiConfig.h"

#include <cstring>

#include <FS.h>
#include <SPIFFS.h>
#include <SD.h>

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
    static Error showFwInfo(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP800
        out << "FW version: FluidNC " << git_info;
        // TODO: change grbl-embedded to FluidNC after fixing WebUI
        out << " # FW target:grbl-embedded  # FW HW:";
        out << (config->_sdCard->get_state() == SDCard::State::NotPresent ? "No SD" : "Direct SD");
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

    static Error SPIFFSSize(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP720
        out << parameter << "SPIFFS  Total:" << formatBytes(SPIFFS.totalBytes());
        out << " Used:" << formatBytes(SPIFFS.usedBytes()) << '\n';
        return Error::Ok;
    }

    static Error formatSpiffs(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP710
        if (strcmp(parameter, "FORMAT") != 0) {
            out << "Parameter must be FORMAT" << '\n';
            return Error::InvalidValue;
        }
        out << "Formatting";
        SPIFFS.format();
        out << "...Done\n";
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

    static Error setSystemMode(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP444
        parameter = trim(parameter);
        if (strcasecmp(parameter, "RESTART") != 0) {
            out << "Parameter must be RESTART" << '\n';
            return Error::InvalidValue;
        }
        log_info("Restarting");
        COMMANDS::restart_MCU();
        return Error::Ok;
    }

    static Error restart(char* parameter, AuthenticationLevel auth_level, Channel& out) {
        parameter = trim(parameter);
        log_info("Restarting");
        COMMANDS::restart_MCU();
        return Error::Ok;
    }

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
        // We do not need the "T=" (type) parameter because the
        // Setting objects know their own type
        if (!split_params(parameter)) {
            return Error::InvalidValue;
        }
        char*       sval = get_param("V", true);
        const char* spos = get_param("P", false);
        if (*spos == '\0') {
            out << "Missing parameter" << '\n';
            return Error::InvalidValue;
        }
        Error ret = do_command_or_setting(spos, sval, auth_level, out);
        return ret;
    }

    static Error listSettings(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP400
        JSONencoder j(false, out);
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
        } catch (Error err) {
            report_status_message(err, out);
            out << "" << '\n';
            return err;
        }
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
        return showFile("/localfs", parameter, auth_level, out);
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

    static Error runLocalFile(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP700
        return runFile("/localfs", parameter, auth_level, out);
    }

    static Error deleteObject(fs::FS fs, char* name, Channel& out) {
        name = trim(name);
        if (*name == '\0') {
            out << "Missing file name!" << '\n';
            return Error::InvalidValue;
        }
        String path = name;
        if (name[0] != '/') {
            path = "/" + path;
        }
        File file2del = fs.open(path);
        if (!file2del) {
            out << "Cannot find file!" << '\n';
            return Error::FsFileNotFound;
        }
        if (file2del.isDirectory()) {
            if (!fs.rmdir(path)) {
                out << "Cannot delete directory! Is directory empty?" << '\n';
                return Error::FsFailedDelDir;
            }
            out << "Directory deleted." << '\n';
        } else {
            if (!fs.remove(path)) {
                out << "Cannot delete file!" << '\n';
                return Error::FsFailedDelFile;
            }
            out << "File deleted." << '\n';
        }
        file2del.close();
        return Error::Ok;
    }

    static Error deleteSDObject(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP215
        auto state = config->_sdCard->begin(SDCard::State::BusyWriting);
        if (state != SDCard::State::Idle) {
            out << (state == SDCard::State::NotPresent ? "No SD card" : "Busy") << '\n';
            return Error::Ok;
        }
        Error res = deleteObject(SD, parameter, out);
        config->_sdCard->end();
        return res;
    }

    static Error deleteLocalFile(char* parameter, AuthenticationLevel auth_level, Channel& out) {
        return deleteObject(SPIFFS, parameter, out);
    }

    static void listDir(fs::FS& fs, const char* dirname, size_t levels, Channel& out) {
        File root = fs.open(dirname);
        if (!root) {
            report_status_message(Error::FsFailedOpenDir, out);
            return;
        }
        if (!root.isDirectory()) {
            report_status_message(Error::FsDirNotFound, out);
            return;
        }
        File file = root.openNextFile();
        while (file) {
            if (file.isDirectory()) {
                if (levels) {
                    listDir(fs, file.name(), levels - 1, out);
                }
            } else {
                allChannels << "[FILE:" << file.name() << "|SIZE:" << file.size() << "]\n";
            }
            file = root.openNextFile();
        }
    }

    static void listFs(fs::FS& fs, const char* fsname, size_t levels, uint64_t totalBytes, uint64_t usedBytes, Channel& out) {
        out << '\n';
        listDir(fs, "/", levels, out);
        out << "[" << fsname;
        out << " Free:" << formatBytes(totalBytes - usedBytes);
        out << " Used:" << formatBytes(usedBytes);
        out << " Total:" << formatBytes(totalBytes);
        out << "]\n";
    }

    static Error listSDFiles(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP210
        switch (config->_sdCard->begin(SDCard::State::BusyReading)) {
            case SDCard::State::Idle:
                break;
            case SDCard::State::NotPresent:
                out << "No SD Card\n";
                return Error::FsFailedMount;
            default:
                out << "SD Card Busy\n";
                return Error::FsFailedBusy;
        }

        listFs(SD, "SD", 10, SD.totalBytes(), SD.usedBytes(), out);
        config->_sdCard->end();
        return Error::Ok;
    }

    static Error listLocalFiles(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // No ESP command
        listFs(SPIFFS, "LocalFS", 10, SPIFFS.totalBytes(), SPIFFS.usedBytes(), out);
        return Error::Ok;
    }

    static void listDirJSON(fs::FS fs, const char* dirname, size_t levels, JSONencoder* j) {
        j->begin_array("files");
        File root = fs.open(dirname);
        File file = root.openNextFile();
        while (file) {
            const char* tailName = strchr(file.name(), '/');
            tailName             = tailName ? tailName + 1 : file.name();
            if (file.isDirectory() && levels) {
                j->begin_array(tailName);
                listDirJSON(fs, file.name(), levels - 1, j);
                j->end_array();
            } else {
                j->begin_object();
                j->member("name", tailName);
                j->member("size", file.size());
                j->end_object();
            }
            file = root.openNextFile();
        }
        j->end_array();
    }

    static Error listLocalFilesJSON(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // No ESP command
        JSONencoder j(false, out);
        j.begin();
        listDirJSON(SPIFFS, "/", 4, &j);
        j.member("total", SPIFFS.totalBytes());
        j.member("used", SPIFFS.usedBytes());
        j.member("occupation", String(long(100 * SPIFFS.usedBytes() / SPIFFS.totalBytes())));
        j.end();
        return Error::Ok;
    }

    static Error showSDStatus(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP200
        const char* resp = "No SD card";
        switch (config->_sdCard->begin(SDCard::State::BusyReading)) {
            case SDCard::State::Idle:
                resp = "SD card detected";
                config->_sdCard->end();
                break;
            case SDCard::State::NotPresent:
                resp = "No SD card";
                break;
            default:
                resp = "Busy";
        }
        out << resp << '\n';
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

        new WebCommand(NULL, WEBCMD, WU, "ESP720", "LocalFS/Size", SPIFFSSize);
        new WebCommand("FORMAT", WEBCMD, WA, "ESP710", "LocalFS/Format", formatSpiffs);
        new WebCommand("path", WEBCMD, WU, "ESP701", "LocalFS/Show", showLocalFile);
        new WebCommand("path", WEBCMD, WU, "ESP700", "LocalFS/Run", runLocalFile);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/List", listLocalFiles);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/ListJSON", listLocalFilesJSON);
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
