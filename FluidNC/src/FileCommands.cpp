// Copyright (c) 2020 Mitch Bradley
// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Settings.h"
#include "WebUI/Authentication.h"
#include "Configuration/JsonGenerator.h"
#include "InputFile.h"    // InputFile
#include "Job.h"          // Job::
#include "xmodem.h"       // xmodemReceive(), xmodemTransmit()
#include "Protocol.h"     // pollingPaused
#include "string_util.h"  // split_prefix()

#include "HashFS.h"

#include <charconv>

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

static Error openFile(const char* fs, const char* parameter, Channel& out, InputFile*& theFile) {
    if (*parameter == '\0') {
        log_string(out, "Missing file name!");
        return Error::InvalidValue;
    }
    std::string path(parameter);
    if (path[0] != '/') {
        path = "/" + path;
    }

    try {
        theFile = new InputFile(fs, path.c_str());
    } catch (Error err) { return err; }
    return Error::Ok;
}

static Error showFile(const char* fs, const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP221
    if (notIdleOrAlarm()) {
        return Error::IdleError;
    }
    InputFile* theFile;
    Error      err;
    if ((err = openFile(fs, parameter, out, theFile)) != Error::Ok) {
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

static Error showSDFile(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP221
    return showFile("sd", parameter, auth_level, out);
}
static Error showLocalFile(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
    return showFile("", parameter, auth_level, out);
}

// This is used by pendants to get partial file contents for preview
static Error fileShowSome(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
    if (notIdleOrAlarm()) {
        return Error::IdleError;
    }
    if (!parameter || !*parameter) {
        log_error_to(out, "Missing argument");
        return Error::InvalidValue;
    }

    std::string_view args(parameter);

    uint32_t firstline = 0;
    uint32_t lastline  = 0;

    std::string_view line_range;
    // Syntax: firstline:lastline,filename  or lastline,filename
    string_util::split_prefix(args, line_range, ',');
    if (line_range.empty() || args.empty()) {
        log_error_to(out, "Invalid syntax");
        return Error::InvalidValue;
    }

    // Args is the list of lines to display
    // N means the first N lines
    // N:M means lines N through M inclusive
    if (line_range.empty()) {
        log_error_to(out, "Missing line count");
        return Error::InvalidValue;
    }
    JSONencoder j(&out, "FileLines");  // Encapsulated JSON

    std::string_view first;
    string_util::split_prefix(line_range, first, ':');
    if (line_range.empty()) {
        firstline = 0;
        std::from_chars(first.data(), first.data() + first.length(), lastline);
    } else {
        std::from_chars(first.data(), first.data() + first.length(), firstline);
        std::from_chars(line_range.data(), line_range.data() + line_range.length(), lastline);
    }
    if (lastline < firstline) {
        log_error_to(out, "Last line is less than first line");
        return Error::InvalidValue;
    }

    const char* error = "";
    j.begin();
    j.begin_array("file_lines");

    InputFile*  theFile;
    Error       err;
    std::string fn(args);
    if ((err = openFile(sdName, fn.c_str(), out, theFile)) != Error::Ok) {
        error = "Cannot open file";
    } else {
        char  fileLine[255];
        Error res = Error::Ok;
        for (uint32_t linenum = 0; linenum < lastline && (res = theFile->readLine(fileLine, 255)) == Error::Ok; ++linenum) {
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

// Can be used by installers to check the version of files
static Error fileShowHash(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
    if (notIdleOrAlarm()) {
        return Error::IdleError;
    }
    if (!parameter || !*parameter) {
        log_error_to(out, "Missing argument");
        return Error::InvalidValue;
    }

    std::string hash = HashFS::hash(parameter);
    replace_string_in_place(hash, "\"", "");
    JSONencoder j(&out, "FileHash");  // Encapsulated JSON
    j.begin();
    j.begin_member_object("signature");
    j.member("algorithm", "SHA2-256");
    j.member("value", hash);
    j.end_object();
    j.member("path", parameter);
    j.end();

    return Error::Ok;
}

static Error fileSendJson(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
    if (notIdleOrAlarm()) {
        return Error::IdleError;
    }
    if (!parameter || !*parameter) {
        log_error_to(out, "Missing argument");
        return Error::InvalidValue;
    }

    std::string fn(parameter);

    if (fn.empty()) {
        log_error_to(out, "Invalid syntax");
        return Error::InvalidValue;
    }

    const char* status = "ok";

    JSONencoder j(&out, "FileContents");  // Encapsulated JSON
    j.begin();
    j.member("cmd", "$File/SendJSON");
    j.member("argument", parameter);

    InputFile* theFile;
    Error      err = Error::Ok;
    if ((err = openFile(localfsName, fn.c_str(), out, theFile)) != Error::Ok) {
        err    = Error::FsFailedOpenFile;
        status = "Cannot open file";
    } else {
        j.begin_member("result");

        char fileLine[101];
        int  len;

        while ((len = theFile->read(fileLine, 100)) > 0) {
            fileLine[len] = '\0';
            // std::string s(fileLine);
            //                replace_string_in_place(s, "\n", "");
            j.verbatim(fileLine);
        }
        delete theFile;
        if (len < 0) {
            status = "File read failed";
            err    = Error::FsFailedRead;
        }
    }
    j.member("status", status);
    j.end();

    return err;
}

static Error runFile(const char* fs, const char* parameter, AuthenticationLevel auth_level, Channel& out) {
    Error err;
    if (state_is(State::Alarm) || state_is(State::ConfigAlarm)) {
        log_string(out, "Alarm");
        return Error::IdleError;
    }
    Job::save();
    InputFile* theFile;
    if ((err = openFile(fs, parameter, out, theFile)) != Error::Ok) {
        Job::restore();
        return err;
    }
    Job::nest(theFile, &out);

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

static Error listFilesystem(const char* fs, const char* value, AuthenticationLevel auth_level, Channel& out) {
    try {
        FluidPath fpath { value, fs };
        auto      iter  = stdfs::recursive_directory_iterator { fpath };
        auto      space = stdfs::space(fpath);
        for (auto const& dir_entry : iter) {
            if (dir_entry.is_directory()) {
                log_stream(out, "[DIR:" << std::string(iter.depth(), ' ') << dir_entry.path().filename().string());
            } else {
                log_stream(out,
                           "[FILE: " << std::string(iter.depth(), ' ') << dir_entry.path().filename().string()
                                     << "|SIZE:" << dir_entry.file_size());
            }
        }
        auto totalBytes = space.capacity;
        auto freeBytes  = space.available;
        auto usedBytes  = totalBytes - freeBytes;
        log_stream(out,
                   "[" << fpath.string() << " Free:" << formatBytes(freeBytes) << " Used:" << formatBytes(usedBytes)
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

static Error listFilesystemJSON(const char* fs, const char* value, AuthenticationLevel auth_level, Channel& out) {
    try {
        FluidPath fpath { value, fs };
        auto      space = stdfs::space(fpath);
        auto      iter  = stdfs::directory_iterator { fpath };

        JSONencoder j(&out);
        j.begin();

        j.begin_array("files");
        for (auto const& dir_entry : iter) {
            j.begin_object();
            j.member("name", dir_entry.path().filename().string());
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

    JSONencoder j(&out, "FilesList");  // Encapsulated JSON

    std::error_code ec;

    FluidPath fpath { parameter, sdName, ec };
    if (ec) {
        error = "No volume";
    }

    j.begin();

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
                if (out.is_visible(fn.stem().string(), fn.extension().string(), is_dir)) {
                    j.begin_object();
                    j.member("name", dir_entry.path().filename().string());
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
            opath += dir_entry.path().filename().string();
            std::string ipath(iDir);
            ipath += "/";
            ipath += dir_entry.path().filename().string();
            log_info_to(out, ipath << " -> " << opath);
            auto err1 = copyFile(ipath.c_str(), opath.c_str(), out);
            if (err1 != Error::Ok) {
                err = err1;
            }
        }
    }
    return err;
}
static Error showLocalFSHashes(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
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

static Error xmodem_receive(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (!value || !*value) {
        value = "uploaded";
    }
    FileStream* outfile;
    try {
        outfile = new FileStream(value, "w");
    } catch (...) {
        out.write(0x18);  // Cancel xmodem transfer with CAN
        log_info("Cannot open " << value);
        return Error::UploadFailed;
    }
    pollingPaused = true;
    bool oldCr    = out.setCr(false);
    delay_ms(1000);
    int len = xmodemReceive(&out, outfile);
    out.setCr(oldCr);
    pollingPaused = false;
    if (len >= 0) {
        log_info("Received " << len << " bytes to file " << outfile->path());
    } else {
        log_info("Reception failed or was canceled");
    }
    std::filesystem::path fname = outfile->fpath();
    delete outfile;
    HashFS::rehash_file(fname);

    return len < 0 ? Error::UploadFailed : Error::Ok;
}

static Error xmodem_send(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (!value || !*value) {
        value = "config.yaml";
    }
    FileStream* infile;
    try {
        infile = new FileStream(value, "r");
    } catch (...) {
        out.write(0x04);  // XModem EOT
        log_info("Cannot open " << value);
        return Error::DownloadFailed;
    }
    bool oldCr = out.setCr(false);
    log_info("Sending " << value << " via XModem");
    int len = xmodemTransmit(&out, infile);
    out.setCr(oldCr);
    delete infile;
    if (len >= 0) {
        log_info("Sent " << len << " bytes");
    } else {
        log_info("Sending failed or was canceled");
    }
    return len < 0 ? Error::DownloadFailed : Error::Ok;
}

static Error restart(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
    log_info("Restarting");
    protocol_send_event(&fullResetEvent);
    return Error::Ok;
}

void make_file_commands() {
    new WebCommand(NULL, WEBCMD, WU, "ESP720", "LocalFS/Size", localFSSize);
    new WebCommand("FORMAT", WEBCMD, WA, "ESP710", "LocalFS/Format", formatLocalFS);
    new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/Show", showLocalFile);
    new WebCommand("path", WEBCMD, WU, "ESP700", "LocalFS/Run", runLocalFile, nullptr);
    new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/List", listLocalFiles, allowConfigStates);
    new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/ListJSON", listLocalFilesJSON, allowConfigStates);
    new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/Delete", deleteLocalFile, allowConfigStates);
    new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/Rename", renameLocalObject, allowConfigStates);
    new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/Backup", backupLocalFS);
    new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/Restore", restoreLocalFS);
    new WebCommand("path", WEBCMD, WU, NULL, "LocalFS/Migrate", migrateLocalFS);
    new WebCommand(NULL, WEBCMD, WU, NULL, "LocalFS/Hashes", showLocalFSHashes);

    new WebCommand("path", WEBCMD, WU, NULL, "File/SendJSON", fileSendJson);
    new WebCommand("path", WEBCMD, WU, NULL, "File/ShowSome", fileShowSome);
    new WebCommand("path", WEBCMD, WU, NULL, "File/ShowHash", fileShowHash);
    new WebCommand("path", WEBCMD, WU, "ESP221", "SD/Show", showSDFile);
    new WebCommand("path", WEBCMD, WU, "ESP220", "SD/Run", runSDFile, nullptr);
    new WebCommand("file_or_directory_path", WEBCMD, WU, "ESP215", "SD/Delete", deleteSDObject);
    new WebCommand("path", WEBCMD, WU, NULL, "SD/Rename", renameSDObject);
    new WebCommand(NULL, WEBCMD, WU, "ESP210", "SD/List", listSDFiles);
    new WebCommand("path", WEBCMD, WU, NULL, "SD/ListJSON", listSDFilesJSON);
    new WebCommand(NULL, WEBCMD, WU, "ESP200", "SD/Status", showSDStatus);
    new WebCommand("path", WEBCMD, WU, NULL, "Files/ListGCode", listGCodeFiles);
    new UserCommand("XR", "Xmodem/Receive", xmodem_receive, allowConfigStates);
    new UserCommand("XS", "Xmodem/Send", xmodem_send, allowConfigStates);

    new WebCommand("RESTART", WEBCMD, WA, NULL, "Bye", restart);
}
