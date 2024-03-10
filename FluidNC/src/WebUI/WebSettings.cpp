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
#include <charconv>

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

    bool get_param(const char* parameter, const char* key, std::string& s) {
        char* start = strstr(parameter, key);
        if (!start) {
            return false;
        }
        s = "";
        for (char* p = start + strlen(key); *p; ++p) {
            if (*p == ' ') {
                break;  // Unescaped space
            }
            if (*p == '\\') {
                if (*++p == '\0') {
                    break;
                }
            }
            s += *p;
        }
        return true;
    }

}

Error WebCommand::action(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
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

    static Error showFwInfoJSON(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP800
        if (strstr(parameter, "json=yes") != NULL) {
            JSONencoder j(true, &out);
            j.begin();
            j.member("cmd", "800");
            j.member("status", "ok");
            j.begin_member_object("data");
            j.member("FWVersion", git_info);
            j.member("FWTarget", "FluidNC");
            j.member("FWTargetId", "60");
            j.member("WebUpdate", "Enabled");

            j.member("Setup", "Disabled");
            j.member("SDConnection", "direct");
            j.member("SerialProtocol", "Socket");
#ifdef ENABLE_AUTHENTICATION
            j.member("Authentication", "Enabled");
#else
            j.member("Authentication", "Disabled");
#endif
            j.member("WebCommunication", "Synchronous");
            j.member("WebSocketIP", "localhost");

            j.member("WebSocketPort", "82");
            j.member("HostName", "fluidnc");
            j.member("WiFiMode", wifi_config.modeName());
            j.member("FlashFileSystem", "LittleFS");
            j.member("HostPath", "/");
            j.member("Time", "none");
            j.member("Axisletters", config->_axes->_names);
            j.end_object();
            j.end();
            return Error::Ok;
        }

        return Error::InvalidStatement;
    }

    static Error showFwInfo(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP800
        if (parameter != NULL && COMMANDS::isJSON(parameter)) {
            return showFwInfoJSON(parameter, auth_level, out);
        }

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

        s << "  # primary sd:";

        (config->_sdCard->config_ok) ? s << "/sd" : s << "none";

        s << " # secondary sd:none ";

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

    static Error localFSSize(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP720
        try {
            auto space      = stdfs::space(FluidPath { "", localfsName });
            auto totalBytes = space.capacity;
            auto freeBytes  = space.available;
            auto usedBytes  = totalBytes - freeBytes;

            log_stream(out, parameter << "LocalFS  Total:" << formatBytes(localfs_size()) << " Used:" << formatBytes(usedBytes));
        } catch (std::filesystem::filesystem_error const& ex) {
            log_error_to(out, ex.what());
            return Error::FsFailedMount;
        }
        return Error::Ok;
    }

    static Error formatLocalFS(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP710
        if (localfs_format(parameter)) {
            return Error::FsFailedFormat;
        }
        log_info("Local filesystem formatted to " << localfsName);
        return Error::Ok;
    }

#ifdef ENABLE_AUTHENTICATION
    static Error setUserPassword(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP555
        if (*parameter == '\0') {
            user_password->setDefault();
            return Error::Ok;
        }
        if (user_password->setStringValue(parameter) != Error::Ok) {
            log_string(out, "Invalid Password");
            return Error::InvalidValue;
        }
        return Error::Ok;
    }
#endif

    static Error restart(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
        log_info("Restarting");
        COMMANDS::restart_MCU();
        return Error::Ok;
    }

    // used by js/restartdlg.js
    static Error setSystemMode(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP444
        // parameter = trim(parameter);
        if (strcasecmp(parameter, "RESTART") != 0) {
            log_string(out, "Parameter must be RESTART");
            return Error::InvalidValue;
        }
        return restart(parameter, auth_level, out);
    }

    // Used by js/statusdlg.js
    static Error showSysStatsJSON(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP420

        JSONencoder j(true, &out);
        j.begin();
        j.member("cmd", "420");
        j.member("status", "ok");
        j.begin_array("data");

        j.begin_object();
        j.member("id", "Chip ID");
        j.member("value", (uint16_t)(ESP.getEfuseMac() >> 32));
        j.end_object();

        j.begin_object();
        j.member("id", "CPU Cores");
        j.member("value", ESP.getChipCores());
        j.end_object();

        std::ostringstream msg;
        msg << ESP.getCpuFreqMHz() << "Mhz";
        j.begin_object();
        j.member("id", "CPU Frequency");
        j.member("value", msg.str());
        j.end_object();

        std::ostringstream msg2;
        msg2 << std::fixed << std::setprecision(1) << temperatureRead() << "°C";
        j.begin_object();
        j.member("id", "CPU Temperature");
        j.member("value", msg2.str());
        j.end_object();

        j.begin_object();
        j.member("id", "Free memory");
        j.member("value", formatBytes(ESP.getFreeHeap()));
        j.end_object();

        j.begin_object();
        j.member("id", "SDK");
        j.member("value", ESP.getSdkVersion());
        j.end_object();

        j.begin_object();
        j.member("id", "Flash Size");
        j.member("value", formatBytes(ESP.getFlashChipSize()));
        j.end_object();

#ifdef ENABLE_WIFI
        WiFiConfig::addWifiStatsToArray(j);
#else
        j.begin_object();
        j.member("id", "Current WiFi Mode");
        j.member("value", "Off");
        j.end_object();
#endif
        // TODO: Mike M - not sure if this is necessary for WebUI since BT is always disabled?
        /*  std::string info = bt_config.info();
        if (info.length()) {
            log_to(out, info);
        }*/

        j.begin_object();
        j.member("id", "FW version");
        j.member("value", "FluidNC " + std::string(git_info));
        j.end_object();

        j.end_array();
        j.end();
        return Error::Ok;
    }

    static Error showSysStats(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP420
        if (COMMANDS::isJSON(parameter)) {
            return showSysStatsJSON(parameter, auth_level, out);
        }

        log_stream(out, "Chip ID: " << (uint16_t)(ESP.getEfuseMac() >> 32));
        log_stream(out, "CPU Cores: " << ESP.getChipCores());
        log_stream(out, "CPU Frequency: " << ESP.getCpuFreqMHz() << "Mhz");

        std::ostringstream msg;
        msg << std::fixed << std::setprecision(1) << temperatureRead() << "°C";
        log_stream(out, "CPU Temperature: " << msg.str());
        log_stream(out, "Free memory: " << formatBytes(ESP.getFreeHeap()));
        log_stream(out, "SDK: " << ESP.getSdkVersion());
        log_stream(out, "Flash Size: " << formatBytes(ESP.getFlashChipSize()));

        // Round baudRate to nearest 100 because ESP32 can say e.g. 115201
        //        log_stream(out, "Baud rate: " << ((Uart0.baud / 100) * 100));

        WiFiConfig::showWifiStats(out);

        std::string info = bt_config.info();
        if (info.length()) {
            log_stream(out, info);
        }
        log_stream(out, "FW version: FluidNC " << git_info);
        return Error::Ok;
    }

    static Error setWebSetting(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP401
        // The string is of the form "P=name T=type V=value
        // We do not need the "T=" (type) parameter because the
        // Setting objects know their own type.  We do not use
        // split_params because if fails if the value string
        // contains '='
        std::string p, v;
        bool        isJSON = COMMANDS::isJSON(parameter);
        if (!(get_param(parameter, "P=", p) && get_param(parameter, "V=", v))) {
            if (isJSON) {
                COMMANDS::send_json_command_response(out, 401, false, errorString(Error::InvalidValue));
            }
            return Error::InvalidValue;
        }

        Error ret = do_command_or_setting(p.c_str(), v.c_str(), auth_level, out);
        if (isJSON) {
            COMMANDS::send_json_command_response(out, 401, ret == Error::Ok, errorString(ret));
        }

        return ret;
    }

    // Used by js/setting.js
    static Error listSettingsJSON(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP400
        JSONencoder j(false, &out);
        j.begin();
        j.member("cmd", "400");
        j.member("status", "ok");
        j.begin_array("data");

        // NVS settings
        j.setCategory("Flash/Settings");
        for (Setting* js : Setting::List) {
            js->addWebui(&j);
        }

        // Configuration tree
        j.setCategory("Running/Config");
        Configuration::JsonGenerator gen(j);
        config->group(gen);

        j.end_array();
        j.end();

        return Error::Ok;
    }

    static Error listSettings(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP400
        if (parameter != NULL) {
            if (strstr(parameter, "json=yes") != NULL) {
                return listSettingsJSON(parameter, auth_level, out);
            }
        }

        JSONencoder j(false, &out);

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

    static Error openFile(const char* fs, const char* parameter, AuthenticationLevel auth_level, Channel& out, InputFile*& theFile) {
        if (*parameter == '\0') {
            log_string(out, "Missing file name!");
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

    static Error showFile(const char* fs, const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP221
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
            // We cannot use the 2-argument form of log_stream() here because
            // fileLine can be overwritten by readLine before the output
            // task has a chance to forward the line to the output channel.
            // The 3-argument form works because it copies the line to a
            // temporary string.
            log_stream(out, fileLine);
        }
        if (res != Error::Eof) {
            log_string(out, errorString(res));
        }
        delete theFile;
        return Error::Ok;
    }

    static bool split(std::string_view input, std::string_view& next, char delim) {
        auto pos = input.find_first_of(delim);
        if (pos != std::string_view::npos) {
            next  = input.substr(pos + 1);
            input = input.substr(0, pos);
            return true;
        }
        next = "";
        return false;
    }

    static Error showSDFile(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP221
        return showFile("sd", parameter, auth_level, out);
    }
    static Error showLocalFile(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
        return showFile("", parameter, auth_level, out);
    }

    // This is used by pendants to get partial file contents for preview
    static Error fileShowSome(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP221
        if (notIdleOrAlarm()) {
            return Error::IdleError;
        }
        if (!parameter || !*parameter) {
            log_error_to(out, "Missing argument");
            return Error::InvalidValue;
        }

        std::string_view args(parameter);

        int firstline = 0;
        int lastline  = 0;

        std::string_view filename;
        split(args, filename, ',');
        if (filename.empty() || args.empty()) {
            log_error_to(out, "Invalid syntax");
            return Error::InvalidValue;
        }

        // Args is the list of lines to display
        // N means the first N lines
        // N:M means lines N through M inclusive
        if (!*parameter) {
            log_error_to(out, "Missing line count");
            return Error::InvalidValue;
        }
        JSONencoder j(true, &out);  // Encapsulated JSON

        std::string_view second;
        split(args, second, ':');
        if (second.empty()) {
            firstline = 0;
            std::from_chars(args.data(), args.data() + args.length(), lastline);
        } else {
            std::from_chars(args.data(), args.data() + args.length(), firstline);
            std::from_chars(second.data(), second.data() + second.length(), lastline);
        }
        const char* error = "";
        j.begin();
        j.begin_array("file_lines");

        InputFile*  theFile;
        Error       err;
        std::string fn(filename);
        if ((err = openFile(sdName, fn.c_str(), auth_level, out, theFile)) != Error::Ok) {
            error = "Cannot open file";
        } else {
            char  fileLine[255];
            Error res;
            for (int linenum = 0; linenum < lastline && (res = theFile->readLine(fileLine, 255)) == Error::Ok; ++linenum) {
                if (linenum >= firstline) {
                    j.string(fileLine);
                }
            }
            delete theFile;
            if (res != Error::Eof && res != Error::Ok) {
                error = errorString(res);
            }
        }
        j.end_array();
        if (*error) {
            j.member("error", error);
        } else {
            j.member("path", fn.c_str());
            j.member("firstline", firstline);
        }

        j.end();
        return Error::Ok;
    }

    static Error runFile(const char* fs, const char* parameter, AuthenticationLevel auth_level, Channel& out) {
        Error err;
        if (sys.state == State::Alarm || sys.state == State::ConfigAlarm) {
            log_string(out, "Alarm");
            return Error::IdleError;
        }
        if (sys.state != State::Idle) {
            log_string(out, "Busy");
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

    static Error runSDFile(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP220
        return runFile("sd", parameter, auth_level, out);
    }

    // Used by js/controls.js
    static Error runLocalFile(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP700
        return runFile("", parameter, auth_level, out);
    }

    static Error deleteObject(const char* fs, const char* name, Channel& out) {
        std::error_code ec;

        if (!name || !*name || (strcmp(name, "/") == 0)) {
            // Disallow deleting everything
            log_error_to(out, "Will not delete everything");
            return Error::InvalidValue;
        }
        try {
            FluidPath fpath { name, fs };
            if (stdfs::is_directory(fpath)) {
                stdfs::remove_all(fpath);
            } else {
                stdfs::remove(fpath);
            }
            HashFS::delete_file(fpath);
        } catch (std::filesystem::filesystem_error const& ex) {
            log_error_to(out, ex.what());
            return Error::FsFailedDelFile;
        }

        return Error::Ok;
    }

    static Error deleteSDObject(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP215
        return deleteObject(sdName, parameter, out);
    }

    static Error deleteLocalFile(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
        return deleteObject(localfsName, parameter, out);
    }

    static Error listFilesystem(const char* fs, const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
        try {
            FluidPath fpath { value, fs };
            auto      iter  = stdfs::recursive_directory_iterator { fpath };
            auto      space = stdfs::space(fpath);
            for (auto const& dir_entry : iter) {
                if (dir_entry.is_directory()) {
                    log_stream(out, "[DIR:" << std::string(iter.depth(), ' ').c_str() << dir_entry.path().filename());
                } else {
                    log_stream(out,
                               "[FILE: " << std::string(iter.depth(), ' ').c_str() << dir_entry.path().filename()
                                         << "|SIZE:" << dir_entry.file_size());
                }
            }
            auto totalBytes = space.capacity;
            auto freeBytes  = space.available;
            auto usedBytes  = totalBytes - freeBytes;
            log_stream(out,
                       "[" << fpath.c_str() << " Free:" << formatBytes(freeBytes) << " Used:" << formatBytes(usedBytes)
                           << " Total:" << formatBytes(totalBytes));
        } catch (std::filesystem::filesystem_error const& ex) {
            log_error_to(out, ex.what());
            return Error::FsFailedMount;
        }

        return Error::Ok;
    }

    static Error listSDFiles(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP210
        return listFilesystem(sdName, parameter, auth_level, out);
    }

    static Error listLocalFiles(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // No ESP command
        return listFilesystem(localfsName, parameter, auth_level, out);
    }

    static Error listFilesystemJSON(const char* fs, const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
        try {
            FluidPath fpath { value, fs };
            auto      space = stdfs::space(fpath);
            auto      iter  = stdfs::directory_iterator { fpath };

            JSONencoder j(false, &out);
            j.begin();

            j.begin_array("files");
            for (auto const& dir_entry : iter) {
                j.begin_object();
                j.member("name", dir_entry.path().filename());
                j.member("size", dir_entry.is_directory() ? -1 : dir_entry.file_size());
                j.end_object();
            }
            j.end_array();

            auto totalBytes = space.capacity;
            auto freeBytes  = space.available;
            auto usedBytes  = totalBytes - freeBytes;

            j.member("path", value);
            j.member("total", formatBytes(totalBytes));
            j.member("used", formatBytes(usedBytes + 1));

            uint32_t percent = totalBytes ? (usedBytes * 100) / totalBytes : 100;

            j.member("occupation", percent);
            j.end();
        } catch (std::filesystem::filesystem_error const& ex) {
            log_error_to(out, ex.what());
            return Error::FsFailedMount;
        }
        return Error::Ok;
    }

    static Error listSDFilesJSON(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP210
        return listFilesystemJSON(sdName, parameter, auth_level, out);
    }

    static Error listLocalFilesJSON(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // No ESP command
        return listFilesystemJSON(localfsName, parameter, auth_level, out);
    }

    // This is used by pendants to get lists of GCode files
    static Error listGCodeFiles(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // No ESP command
        const char* error = "";

        JSONencoder j(true, &out);  // Encapsulated JSON
        j.begin();

        std::error_code ec;

        FluidPath fpath { parameter, sdName, ec };
        if (ec) {
            error = "No volume";
        }

        j.begin_array("files");
        if (!*error) {  // Array is empty for failure to open the volume
            auto iter = stdfs::directory_iterator { fpath, ec };
            if (ec) {
                // Array is empty for failure to open the path
                error = "Bad path";
            } else {
                for (auto const& dir_entry : iter) {
                    auto fn     = dir_entry.path().filename();
                    auto is_dir = dir_entry.is_directory();
                    if (out.is_visible(fn.stem(), fn.extension(), is_dir)) {
                        j.begin_object();
                        j.member("name", dir_entry.path().filename());
                        j.member("size", is_dir ? -1 : dir_entry.file_size());
                        j.end_object();
                    }
                }
            }
        }
        j.end_array();

        j.member("path", parameter);
        if (*error) {
            j.member("error", error);
        }

#if 0
        // Don't include summary information because it can take a long
        // time to calculate for large volumes
        auto space = stdfs::space(fpath, ec);
        if (!ec) {
            auto totalBytes = space.capacity;
            auto freeBytes  = space.available;
            auto usedBytes  = totalBytes - freeBytes;

            j.member("total", formatBytes(totalBytes));
            j.member("used", formatBytes(usedBytes + 1));

            uint32_t percent = totalBytes ? (usedBytes * 100) / totalBytes : 100;

            j.member("occupation", percent);
        }
#endif
        j.end();
        return Error::Ok;
    }

    static Error renameObject(const char* fs, const char* parameter, AuthenticationLevel auth_level, Channel& out) {
        if (!parameter || *parameter == '\0') {
            return Error::InvalidValue;
        }
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
            HashFS::rename_file(inPath, outPath, true);
        } catch (std::filesystem::filesystem_error const& ex) {
            log_error_to(out, ex.what());
            return Error::FsFailedRenameFile;
        }
        return Error::Ok;
    }
    static Error renameSDObject(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
        return renameObject(sdName, parameter, auth_level, out);
    }
    static Error renameLocalObject(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
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
            log_error_to(out, "Cannot create file " << opath);
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
                log_error_to(out, "Not handling localfs subdirectories");
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
    static Error showLocalFSHashes(const char* parameter, WebUI::AuthenticationLevel auth_level, Channel& out) {
        for (const auto& [name, hash] : HashFS::localFsHashes) {
            log_info_to(out, name << ": " << hash);
        }
        return Error::Ok;
    }

    static Error backupLocalFS(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // No ESP command
        return copyDir("/localfs", "/sd/localfs", out);
    }
    static Error restoreLocalFS(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // No ESP command
        return copyDir("/sd/localfs", "/localfs", out);
    }
    static Error migrateLocalFS(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // No ESP command
        const char* newfs = parameter && *parameter ? parameter : "littlefs";
        if (strcmp(newfs, localfsName) == 0) {
            log_error_to(out, "localfs format is already " << newfs);
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
    static Error showSDStatus(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP200
        try {
            FluidPath path { "", "/sd" };
        } catch (std::filesystem::filesystem_error const& ex) {
            log_error_to(out, ex.what());
            log_string(out, "No SD card detected");
            return Error::FsFailedMount;
        }
        log_string(out, "SD card detected");
        return Error::Ok;
    }

    static Error setRadioState(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP115
        // parameter = trim(parameter);
        if (*parameter == '\0') {
            // Display the radio state
            bool on = wifi_config.isOn() || bt_config.isOn();
            log_string(out, on ? "ON" : "OFF");
            return Error::Ok;
        }
        int8_t on = -1;
        if (strcasecmp(parameter, "ON") == 0) {
            on = 1;
        } else if (strcasecmp(parameter, "OFF") == 0) {
            on = 0;
        }
        if (on == -1) {
            log_string(out, "only ON or OFF mode supported!");
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
    static Error showWebHelp(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP0
        log_string(out, "Persistent web settings - $name to show, $name=value to set");
        log_string(out, "ESPname FullName         Description");
        log_string(out, "------- --------         -----------");

        for (Setting* setting : Setting::List) {
            if (setting->getType() == WEBSET) {
                log_stream(out,
                           LeftJustify(setting->getGrblName() ? setting->getGrblName() : "", 8)
                               << LeftJustify(setting->getName(), 25 - 8) << setting->getDescription());
            }
        }
        log_string(out, "");
        log_string(out, "Other web commands: $name to show, $name=value to set");
        log_string(out, "ESPname FullName         Values");
        log_string(out, "------- --------         ------");

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
        user_password  = new AuthPasswordSetting("User password", "WebUI/UserPassword", DEFAULT_USER_PWD);
        admin_password = new AuthPasswordSetting("Admin password", "WebUI/AdminPassword", DEFAULT_ADMIN_PWD);
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
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/Show", showLocalFile);
        new WebCommand("path", WEBCMD, WU, "ESP700", "LocalFS/Run", runLocalFile);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/List", listLocalFiles);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/ListJSON", listLocalFilesJSON);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/Delete", deleteLocalFile);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/Rename", renameLocalObject);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/Backup", backupLocalFS);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/Restore", restoreLocalFS);
        new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/Migrate", migrateLocalFS);
        new WebCommand(NULL, WEBCMD, WU, NULL, "LocalFS/Hashes", showLocalFSHashes);

        new WebCommand("path", WEBCMD, WU, NULL, "File/ShowSome", fileShowSome);
        new WebCommand("path", WEBCMD, WU, "ESP221", "SD/Show", showSDFile);
        new WebCommand("path", WEBCMD, WU, "ESP220", "SD/Run", runSDFile);
        new WebCommand("file_or_directory_path", WEBCMD, WU, "ESP215", "SD/Delete", deleteSDObject);
        new WebCommand("path", WEBCMD, WU, NULL, "SD/Rename", renameSDObject);
        new WebCommand(NULL, WEBCMD, WU, "ESP210", "SD/List", listSDFiles);
        new WebCommand("path", WEBCMD, WU, NULL, "SD/ListJSON", listSDFilesJSON);
        new WebCommand(NULL, WEBCMD, WU, "ESP200", "SD/Status", showSDStatus);

        new WebCommand("path", WEBCMD, WU, NULL, "Files/ListGCode", listGCodeFiles);

        new WebCommand("ON|OFF", WEBCMD, WA, "ESP115", "Radio/State", setRadioState);

        new WebCommand("P=position T=type V=value", WEBCMD, WA, "ESP401", "WebUI/Set", setWebSetting);
        new WebCommand(NULL, WEBCMD, WU, "ESP400", "WebUI/List", listSettings, anyState);
        new WebCommand(NULL, WEBCMD, WG, "ESP0", "WebUI/Help", showWebHelp, anyState);
        new WebCommand(NULL, WEBCMD, WG, "ESP", "WebUI/Help", showWebHelp, anyState);
    }
}
