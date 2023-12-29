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
#include "../InputFile.h"  // InputFile

#include "Commands.h"  // COMMANDS::restart_MCU();
#include "WifiConfig.h"

#include "src/HashFS.h"

#include <cstring>
#include <sstream>
#include <iomanip>

namespace WebUI {

    static std::string LeftJustify(const char* s, size_t width) {
        std::string ret(s);

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
        LogStream s(out, "FW version: FluidNC ");
        s << git_info;
        // TODO: change grbl-embedded to FluidNC after fixing WebUI
        s << " # FW target:grbl-embedded  # FW HW:";

        // std::error_code ec;
        // FluidPath { "/sd", ec };
        // s << (ec ? "No SD" : "Direct SD");

        // We do not check the SD presence here because if the SD card is out,
        // WebUI will switch to M20 for SD access, which is wrong for FluidNC
        s << "Direct SD";

        s << "  # primary sd:/sd # secondary sd:none ";

        s << " # authentication:";
#ifdef ENABLE_AUTHENTICATION
        s << "yes";
#else
        s << "no";
#endif
        s << wifi_config.webInfo();

        //to save time in decoding `?`
        s << " # axis:" << config->_axes->_numberAxis;
        return Error::Ok;
    }

    static Error localFSSize(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP720
        std::error_code ec;

        auto space = stdfs::space(FluidPath { "", localfsName, ec }, ec);
        if (ec) {
            log_to(out, "Error ", ec.message());
            return Error::FsFailedMount;
        }
        auto totalBytes = space.capacity;
        auto freeBytes  = space.available;
        auto usedBytes  = totalBytes - freeBytes;

        log_to(out, parameter, "LocalFS  Total:" << formatBytes(localfs_size()) << " Used:" << formatBytes(usedBytes));
        return Error::Ok;
    }

    static Error formatLocalFS(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP710
        if (localfs_format(parameter)) {
            return Error::FsFailedFormat;
        }
        log_info("Local filesystem formatted to " << localfsName);
        return Error::Ok;
    }

#ifdef ENABLE_AUTHENTICATION
    static Error setUserPassword(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP555
        if (*parameter == '\0') {
            user_password->setDefault();
            return Error::Ok;
        }
        if (user_password->setStringValue(parameter) != Error::Ok) {
            log_to(out, "Invalid Password");
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
            log_to(out, "Parameter must be RESTART");
            return Error::InvalidValue;
        }
        return restart(parameter, auth_level, out);
    }

    // Used by js/statusdlg.js
    static Error showSysStats(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP420
        log_to(out, "Chip ID: ", (uint16_t)(ESP.getEfuseMac() >> 32));
        log_to(out, "CPU Cores: ", ESP.getChipCores());
        log_to(out, "CPU Frequency: ", ESP.getCpuFreqMHz() << "Mhz");
        std::ostringstream msg;
        msg << std::fixed << std::setprecision(1) << temperatureRead() << "°C";
        log_to(out, "CPU Temperature: ", msg.str());
        log_to(out, "Free memory: ", formatBytes(ESP.getFreeHeap()));
        log_to(out, "SDK: ", ESP.getSdkVersion());
        log_to(out, "Flash Size: ", formatBytes(ESP.getFlashChipSize()));

        // Round baudRate to nearest 100 because ESP32 can say e.g. 115201
        //        log_to(out, "Baud rate: ", ((Uart0.baud / 100) * 100));

        WiFiConfig::showWifiStats(out);

        std::string info = bt_config.info();
        if (info.length()) {
            log_to(out, info);
        }
        log_to(out, "FW version: FluidNC ", git_info);
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
        JSONencoder j(true, &out);
        j.begin();
        j.begin_array("EEPROM");

        // NVS settings
        j.setCategory("nvs");
        for (Setting* js : Setting::List) {
            js->addWebui(&j);
        }

        // Configuration tree
        j.setCategory("tree");
        Configuration::JsonGenerator gen(j);
        config->group(gen);

        j.end_array();
        j.end();

        return Error::Ok;
    }

    static Error openFile(const char* fs, char* parameter, AuthenticationLevel auth_level, Channel& out, InputFile*& theFile) {
        if (*parameter == '\0') {
            log_to(out, "Missing file name!");
            return Error::InvalidValue;
        }
        std::string path(parameter);
        if (path[0] != '/') {
            path = "/" + path;
        }

        try {
            theFile = new InputFile(fs, path.c_str(), auth_level, out);
        } catch (Error err) { return err; }
        return Error::Ok;
    }

    static Error showFile(const char* fs, char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP221
        if (notIdleOrAlarm()) {
            return Error::IdleError;
        }
        InputFile* theFile;
        Error      err;
        if ((err = openFile(fs, parameter, auth_level, out, theFile)) != Error::Ok) {
            return err;
        }
        char  fileLine[255];
        Error res;
        while ((res = theFile->readLine(fileLine, 255)) == Error::Ok) {
            // We cannot use the 2-argument form of log_to() here because
            // fileLine can be overwritten by readLine before the output
            // task has a chance to forward the line to the output channel.
            // The 3-argument form works because it copies the line to a
            // temporary string.
            log_to(out, "", fileLine);
        }
        if (res != Error::Eof) {
            log_to(out, errorString(res));
        }
        delete theFile;
        return Error::Ok;
    }

    static Error showSDFile(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP221
        return showFile("sd", parameter, auth_level, out);
    }
    static Error showLocalFile(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP701
        return showFile("", parameter, auth_level, out);
    }

    static Error runFile(const char* fs, char* parameter, AuthenticationLevel auth_level, Channel& out) {
        Error err;
        if (sys.state == State::Alarm || sys.state == State::ConfigAlarm) {
            log_to(out, "Alarm");
            return Error::IdleError;
        }
        if (sys.state != State::Idle) {
            log_to(out, "Busy");
            return Error::IdleError;
        }
        InputFile* theFile;
        if ((err = openFile(fs, parameter, auth_level, out, theFile)) != Error::Ok) {
            return err;
        }
        allChannels.registration(theFile);

        //report_realtime_status(out);
        return Error::Ok;
    }

    static Error runSDFile(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP220
        return runFile("sd", parameter, auth_level, out);
    }

    // Used by js/controls.js
    static Error runLocalFile(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP700
        return runFile("", parameter, auth_level, out);
    }

    static Error deleteObject(const char* fs, char* name, Channel& out) {
        std::error_code ec;

        FluidPath fpath { name, fs, ec };
        if (ec) {
            log_to(out, "No SD");
            return Error::FsFailedMount;
        }
        auto isDir = stdfs::is_directory(fpath, ec);
        if (ec) {
            log_to(out, "Delete failed: ", ec.message());
            return Error::FsFileNotFound;
        }
        if (isDir) {
            stdfs::remove_all(fpath, ec);
            if (ec) {
                log_to(out, "Delete Directory failed: ", ec.message());
                return Error::FsFailedDelDir;
            }
        } else {
            stdfs::remove(fpath, ec);
            if (ec) {
                log_to(out, "Delete File failed: ", ec.message());
                return Error::FsFailedDelFile;
            }
        }
        HashFS::delete_file(fpath);

        return Error::Ok;
    }

    static Error deleteSDObject(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP215
        return deleteObject(sdName, parameter, out);
    }

    static Error deleteLocalFile(char* parameter, AuthenticationLevel auth_level, Channel& out) {
        return deleteObject(localfsName, parameter, out);
    }

    static Error listFilesystem(const char* fs, const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
        std::error_code ec;

        FluidPath fpath { value, fs, ec };
        if (ec) {
            log_to(out, "No SD card");
            return Error::FsFailedMount;
        }

        auto iter = stdfs::recursive_directory_iterator { fpath, ec };
        if (ec) {
            log_to(out, "Error: ", ec.message());
            return Error::FsFailedMount;
        }
        for (auto const& dir_entry : iter) {
            if (dir_entry.is_directory()) {
                log_to(out, "[DIR:", std::string(iter.depth(), ' ').c_str() << dir_entry.path().filename());
            } else {
                log_to(out,
                       "[FILE: ",
                       std::string(iter.depth(), ' ').c_str() << dir_entry.path().filename() << "|SIZE:" << dir_entry.file_size());
            }
        }
        auto space = stdfs::space(fpath, ec);
        if (ec) {
            log_to(out, "Error ", ec.value() << " " << ec.message());
            return Error::FsFailedMount;
        }

        auto totalBytes = space.capacity;
        auto freeBytes  = space.available;
        auto usedBytes  = totalBytes - freeBytes;
        log_to(out,
               "[",
               fpath.c_str() << " Free:" << formatBytes(freeBytes) << " Used:" << formatBytes(usedBytes)
                             << " Total:" << formatBytes(totalBytes));
        return Error::Ok;
    }

    static Error listSDFiles(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP210
        return listFilesystem(sdName, parameter, auth_level, out);
    }

    static Error listLocalFiles(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // No ESP command
        return listFilesystem(localfsName, parameter, auth_level, out);
    }

    static Error listFilesystemJSON(const char* fs, const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
        std::error_code ec;

        FluidPath fpath { value, fs, ec };
        if (ec) {
            log_to(out, "No SD card");
            return Error::FsFailedMount;
        }

        JSONencoder j(false, &out);
        j.begin();

        auto iter = stdfs::directory_iterator { fpath, ec };
        if (ec) {
            log_to(out, "Error: ", ec.message());
            return Error::FsFailedMount;
        }
        j.begin_array("files");
        for (auto const& dir_entry : iter) {
            j.begin_object();
            j.member("name", dir_entry.path().filename());
            j.member("size", dir_entry.is_directory() ? -1 : dir_entry.file_size());
            j.end_object();
        }
        j.end_array();

        auto space = stdfs::space(fpath, ec);
        if (ec) {
            log_to(out, "Error ", ec.value() << " " << ec.message());
            return Error::FsFailedMount;
        }

        auto totalBytes = space.capacity;
        auto freeBytes  = space.available;
        auto usedBytes  = totalBytes - freeBytes;

        j.member("path", value);
        j.member("total", formatBytes(totalBytes));
        j.member("used", formatBytes(usedBytes + 1));

        uint32_t percent = totalBytes ? (usedBytes * 100) / totalBytes : 100;

        j.member("occupation", percent);
        j.end();
        return Error::Ok;
    }

    static Error listSDFilesJSON(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP210
        return listFilesystemJSON(sdName, parameter, auth_level, out);
    }

    static Error listLocalFilesJSON(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // No ESP command
        return listFilesystemJSON(localfsName, parameter, auth_level, out);
    }

    static Error renameObject(const char* fs, char* parameter, AuthenticationLevel auth_level, Channel& out) {
        auto opath = strchr(parameter, '>');
        if (*opath == '\0') {
            return Error::InvalidValue;
        }
        const char* ipath = parameter;
        *opath++          = '\0';
        try {
            FluidPath inPath { ipath, fs };
            FluidPath outPath { opath, fs };
            std::filesystem::rename(inPath, outPath);
        } catch (const Error err) {
            log_error_to(out, "Cannot rename " << ipath << " to " << opath);
            return Error::FsFailedRenameFile;
        }
        return Error::Ok;
    }
    static Error renameSDObject(char* parameter, AuthenticationLevel auth_level, Channel& out) {
        return renameObject(sdName, parameter, auth_level, out);
    }
    static Error renameLocalObject(char* parameter, AuthenticationLevel auth_level, Channel& out) {
        return renameObject(localfsName, parameter, auth_level, out);
    }

    static Error copyFile(const char* ipath, const char* opath, Channel& out) {  // No ESP command
        std::filesystem::path filepath;
        try {
            FileStream outFile { opath, "w" };
            FileStream inFile { ipath, "r" };
            uint8_t    buf[512];
            size_t     len;
            while ((len = inFile.read(buf, 512)) > 0) {
                outFile.write(buf, len);
            }
            filepath = outFile.fpath();
        } catch (const Error err) {
            log_error("Cannot create file " << opath);
            return Error::FsFailedCreateFile;
        }
        // Rehash after outFile goes out of scope
        HashFS::rehash_file(filepath);
        return Error::Ok;
    }
    static Error copyDir(const char* iDir, const char* oDir, Channel& out) {  // No ESP command
        std::error_code ec;

        {  // Block to manage scope of outDir
            FluidPath outDir { oDir, "", ec };
            if (ec) {
                log_error_to(out, "Cannot mount /sd");
                return Error::FsFailedMount;
            }

            if (outDir.hasTail()) {
                stdfs::create_directory(outDir, ec);
                if (ec) {
                    log_error_to(out, "Cannot create " << oDir);
                    return Error::FsFailedOpenDir;
                }
            }
        }

        FluidPath fpath { iDir, "", ec };
        if (ec) {
            log_error_to(out, "Cannot open " << iDir);
            return Error::FsFailedMount;
        }

        auto iter = stdfs::directory_iterator { fpath, ec };
        if (ec) {
            log_error_to(out, fpath << " " << ec.message());
            return Error::FsFailedMount;
        }
        Error err = Error::Ok;
        for (auto const& dir_entry : iter) {
            if (dir_entry.is_directory()) {
                log_error("Not handling localfs subdirectories");
            } else {
                std::string opath(oDir);
                opath += "/";
                opath += dir_entry.path().filename().c_str();
                std::string ipath(iDir);
                ipath += "/";
                ipath += dir_entry.path().filename().c_str();
                log_info_to(out, ipath << " -> " << opath);
                auto err1 = copyFile(ipath.c_str(), opath.c_str(), out);
                if (err1 != Error::Ok) {
                    err = err1;
                }
            }
        }
        return err;
    }
    static Error showLocalFSHashes(char* parameter, WebUI::AuthenticationLevel auth_level, Channel& out) {
        for (const auto& [name, hash] : HashFS::localFsHashes) {
            log_info_to(out, name << ": " << hash);
        }
        return Error::Ok;
    }

    static Error backupLocalFS(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // No ESP command
        return copyDir("/localfs", "/sd/localfs", out);
    }
    static Error restoreLocalFS(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // No ESP command
        return copyDir("/sd/localfs", "/localfs", out);
    }
    static Error migrateLocalFS(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // No ESP command
        const char* newfs = parameter && *parameter ? parameter : "littlefs";
        if (strcmp(newfs, localfsName) == 0) {
            log_error("localfs format is already " << newfs);
            return Error::InvalidValue;
        }
        log_info("Backing up local filesystem contents to SD");
        Error err = copyDir("/localfs", "/sd/localfs", out);
        if (err != Error::Ok) {
            return err;
        }
        log_info("Reformatting local filesystem to " << newfs);
        if (localfs_format(newfs)) {
            return Error::FsFailedFormat;
        }
        log_info("Restoring local filesystem contents");
        return copyDir("/sd/localfs", "/localfs", out);
    }

    // Used by js/files.js
    static Error showSDStatus(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP200
        std::error_code ec;

        FluidPath path { "", "/sd", ec };
        if (ec) {
            log_to(out, "No SD card");
            return Error::FsFailedMount;
        }
        log_to(out, "SD card detected");
        return Error::Ok;
    }

    static Error setRadioState(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP115
        parameter = trim(parameter);
        if (*parameter == '\0') {
            // Display the radio state
            bool on = wifi_config.isOn() || bt_config.isOn();
            log_to(out, on ? "ON" : "OFF");
            return Error::Ok;
        }
        int8_t on = -1;
        if (strcasecmp(parameter, "ON") == 0) {
            on = 1;
        } else if (strcasecmp(parameter, "OFF") == 0) {
            on = 0;
        }
        if (on == -1) {
            log_to(out, "only ON or OFF mode supported!");
            return Error::InvalidValue;
        }

        //Stop everything
        wifi_config.end();
        bt_config.end();

        //if On start proper service
        if (on && (wifi_config.begin() || bt_config.begin())) {
            return Error::Ok;
        }
        log_msg_to(out, "Radio is Off");
        return Error::Ok;
    }

    static Error showWebHelp(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP0
        log_to(out, "Persistent web settings - $name to show, $name=value to set");
        log_to(out, "ESPname FullName         Description");
        log_to(out, "------- --------         -----------");
        ;
        for (Setting* setting : Setting::List) {
            if (setting->getType() == WEBSET) {
                log_to(out,
                       "",
                       LeftJustify(setting->getGrblName() ? setting->getGrblName() : "", 8)
                           << LeftJustify(setting->getName(), 25 - 8) << setting->getDescription());
            }
        }
        log_to(out, "");
        log_to(out, "Other web commands: $name to show, $name=value to set");
        log_to(out, "ESPname FullName         Values");
        log_to(out, "------- --------         ------");

        for (Command* cp : Command::List) {
            if (cp->getType() == WEBCMD) {
                LogStream s(out, "");
                s << LeftJustify(cp->getGrblName() ? cp->getGrblName() : "", 8) << LeftJustify(cp->getName(), 25 - 8);
                if (cp->getDescription()) {
                    s << cp->getDescription();
                }
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

        new WebCommand(NULL, WEBCMD, WU, "ESP720", "LocalFS/Size", localFSSize);
        new WebCommand("FORMAT", WEBCMD, WA, "ESP710", "LocalFS/Format", formatLocalFS);
        new WebCommand("path", WEBCMD, WU, "ESP701", "LocalFS/Show", showLocalFile);
        new WebCommand("path", WEBCMD, WU, "ESP700", "LocalFS/Run", runLocalFile);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/List", listLocalFiles);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/ListJSON", listLocalFilesJSON);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/Delete", deleteLocalFile);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/Rename", renameLocalObject);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/Backup", backupLocalFS);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/Restore", restoreLocalFS);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/Migrate", migrateLocalFS);
        new WebCommand(NULL, WEBCMD, WU, NULL, "LocalFS/Hashes", showLocalFSHashes);

        new WebCommand("path", WEBCMD, WU, "ESP221", "SD/Show", showSDFile);
        new WebCommand("path", WEBCMD, WU, "ESP220", "SD/Run", runSDFile);
        new WebCommand("file_or_directory_path", WEBCMD, WU, "ESP215", "SD/Delete", deleteSDObject);
        new WebCommand("path", WEBCMD, WU, NULL, "SD/Rename", renameSDObject);
        new WebCommand(NULL, WEBCMD, WU, "ESP210", "SD/List", listSDFiles);
        new WebCommand("path", WEBCMD, WU, NULL, "SD/ListJSON", listSDFilesJSON);
        new WebCommand(NULL, WEBCMD, WU, "ESP200", "SD/Status", showSDStatus);

        new WebCommand("ON|OFF", WEBCMD, WA, "ESP115", "Radio/State", setRadioState);

        new WebCommand("P=position T=type V=value", WEBCMD, WA, "ESP401", "WebUI/Set", setWebSetting);
        new WebCommand(NULL, WEBCMD, WU, "ESP400", "WebUI/List", listSettings, anyState);
        new WebCommand(NULL, WEBCMD, WG, "ESP0", "WebUI/Help", showWebHelp, anyState);
        new WebCommand(NULL, WEBCMD, WG, "ESP", "WebUI/Help", showWebHelp, anyState);
    }
}
