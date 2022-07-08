#include "FileSystem.h"
#include "SDCard.h"
#include "Machine/MachineConfig.h"  // config
#include "Uart.h"                   // Uart0
#include <sys/stat.h>

#include <SD.h>
#include <SPIFFS.h>
#include <LittleFS.h>

FileSystem::FsInfo FileSystem::filesystems[] = { { "/sd", 0, true, (FS&)SD, openSD, closeSD },
                                                 { "/spiffs", 1, false, (FS&)SPIFFS, NULL, NULL },
                                                 { "/littlefs", 2, true, (FS&)LittleFS, NULL, NULL } };

void FileSystem::joinPath(const String& path, const FsInfo& fs) {
    const FsInfo* realFs = nullptr;

    if (path.startsWith("/")) {
        // The path might have a filesystem prefix
        size_t slashLoc = path.indexOf("/", 1);
        auto   first    = slashLoc == -1 ? path : path.substring(0, slashLoc);

        if (first.equalsIgnoreCase("/localfs")) {
            realFs = &FileSystem::localfs;
        } else {
            for (const FileSystem::FsInfo& fs : FileSystem::filesystems) {
                if (first.equalsIgnoreCase(fs.name)) {
                    realFs = &fs;
                    break;
                }
            }
        }

        if (realFs) {
            // The path starts with a filesystem name
            if (realFs->name == first) {
                // The path's filesystem name exactly matches a
                // filesystem name, so use the path verbatim
                _fspath = path;
            } else {
                // The path's filesystem name matches, but differs
                // in case, so replace it with the canoncal name
                _fspath = realFs->name;
                if (slashLoc != -1) {
                    _fspath += path.substring(slashLoc);
                }
            }
        } else {
            // The path does not start with a filesystem name, so
            // prepend the canonical version of fsname
            realFs  = &fs;
            _fspath = realFs->name + path;
        }
    } else {
        // The path does not begin with / so it cannot have a filesystem prefix.
        // prepend the canonical version of fsname
        realFs  = &fs;
        _fspath = realFs->name + "/" + path;
    }
    // Some of the underlying file functions like opendir() fail
    // with paths that end in /
    if (_fspath.endsWith("/")) {
        _fspath = _fspath.substring(0, _fspath.length() - 1);
    }

    _realFs     = realFs;
    _hasSubdirs = realFs->hasSubdirs;
    _fsindex    = realFs->fsindex;
}

String FileSystem::joinFile(const String& path, const String& filename) {
    String fullPath = path;
    if (path.endsWith("/")) {
        return filename.startsWith("/") ? path.substring(0, path.length() - 1) + filename : path + filename;
    } else {
        return filename.startsWith("/") ? path + filename : path + "/" + filename;
    }
}

void FileSystem::openSD() {
    switch (config->_sdCard->begin(SDCard::State::Busy)) {
        case SDCard::State::Idle:
            break;
        case SDCard::State::Busy:
        case SDCard::State::BusyUploading:
        case SDCard::State::BusyParsing:
        case SDCard::State::BusyWriting:
        case SDCard::State::BusyReading:
            throw Error::FsFailedBusy;
        case SDCard::State::NotPresent:
            throw Error::FsFailedMount;
    }
}
void FileSystem::closeSD() {
    config->_sdCard->end();
}

FileSystem::FileSystem(const String& path, const FsInfo& fs) {
    joinPath(path, fs);

    if (_realFs->openfn) {
        _realFs->openfn();
    }
}

FileSystem::~FileSystem() {
    if (_realFs->closefn) {
        _realFs->closefn();
    }
}

FS& FileSystem::theFS() {
    return _realFs->theFS;
    //    return _fsindex == 0 ? (FS&)SD : (FS&)SPIFFS;
}

// The Arduino FS class does not include totalBytes() and usedBytes() methods;
// they are in the derived classes SPIFFS and SD, with different return types.

uint64_t FileSystem::totalBytes() {
    switch (_fsindex) {
        case 0:
            return SD.totalBytes();
        case 1:
            return (uint64_t)SPIFFS.totalBytes();
        case 2:
            return LittleFS.totalBytes();
        default:
            return 0;
    }
}
uint64_t FileSystem::usedBytes() {
    switch (_fsindex) {
        case 0:
            return SD.usedBytes();
        case 1:
            return (uint64_t)SPIFFS.usedBytes();
        case 2:
            return LittleFS.usedBytes();
        default:
            return 0;
    }
}

bool FileSystem::format() {
    switch (_fsindex) {
        case 0:
            return false;
        case 1:
            return SPIFFS.format();
        case 2:
            return LittleFS.format();
        default:
            return 0;
    }
}

void FileSystem::listDirJSON(const String& path, size_t levels, WebUI::JSONencoder& j) {
    j.begin_array("files");

    String xpath = path;
    if (xpath.endsWith("/")) {
        xpath = xpath.substring(0, xpath.length() - 1);
    }
    DIR* dir = opendir(xpath.c_str());
    while (dir) {
        struct dirent* file = readdir(dir);
        if (!file) {
            break;
        }
        String fullPath = path.endsWith("/") ? path + file->d_name : path + "/" + file->d_name;
        bool   isDir    = file->d_type == DT_DIR;
        if (isDir && levels) {
            j.begin_array(path.c_str());
            listDirJSON(fullPath, levels - 1, j);
            j.end_array();
        } else {
            j.begin_object();
            j.member("name", file->d_name);
            struct stat statbuf;
            stat(fullPath.c_str(), &statbuf);
            j.member("size", isDir ? -1 : statbuf.st_size);
            // Displaying file date and time correctly is a lot of
            // trouble because of different formats, time zones, etc.
            // Furthermore it is often wrong for removable devices on
            // embedded systems, which often lack a realtime clock.
            // FluidNC systems rarely have one.
            j.member("datetime", "");
            j.end_object();
        }
    }
    j.end_array();
}

void FileSystem::listJSON(const String& status, Print& out) {
    WebUI::JSONencoder j(true, out);
    j.begin();
    listDirJSON(_fspath, 0, j);
    j.member("path", _fspath);
    j.member("status", status);
    size_t total = totalBytes();
    size_t used  = usedBytes();
    j.member("total", formatBytes(total));
    j.member("used", formatBytes(used));
    j.member("occupation", String(100 * used / total));
    j.end();
}

void FileSystem::listDir(const String& path, String indent, size_t levels, Print& out) {
    DIR* dir = opendir(path.c_str());
    while (dir) {
        struct dirent* file = readdir(dir);
        if (!file) {
            break;
        }
        const char* name     = file->d_name;
        String      fullPath = path.endsWith("/") ? path + name : path + "/" + name;
        //        log_info(file.name() << " " << tailName);
        if (file->d_type == DT_DIR) {
            if (levels) {
                out << "[Dir: " << indent << name << "]\n";
                listDir(fullPath, indent + " ", levels - 1, out);
            }
        } else {
            struct stat statbuf;
            stat(fullPath.c_str(), &statbuf);
            out << "[FILE:" << indent << name << "|SIZE:" << (int)statbuf.st_size << "]\n";
        }
    }
}
void FileSystem::list(Print& out) {
    out << '\n';
    listDir(_fspath, "", 10, out);
    out << "[" << _fspath;
    out << " Free:" << formatBytes(totalBytes() - usedBytes());
    out << " Used:" << formatBytes(usedBytes());
    out << " Total:" << formatBytes(totalBytes());
    out << "]\n";
}

bool FileSystem::mkdir() {
    // We would naively hope that .mkdir would fail on filesystems
    // that do not support subdirectories, but due to a subtle SPIFF
    // bug, that is not the case.  The lowest level opendir() function
    // in the ESP IDK ignores the name argument and always succeeds,
    // which causes the Arduino FS framework to think that mkdir is
    // unnecessary and successful.
    //        return _hasSubdirs && theFS().mkdir(_file == "" ? _dir : _path);
    return _hasSubdirs && ::mkdir(_fspath.c_str(), 0666) == 0;
    //    return _hasSubdirs && theFS().mkdir(_fspath);
}
bool FileSystem::mkdir(const String& filename) {
    return _hasSubdirs && ::mkdir(joinFile(_fspath, filename).c_str(), 0666) == 0;
}

bool FileSystem::deleteFile() {
    return unlink(_fspath.c_str()) == 0;
}
bool FileSystem::deleteFile(const String& filename) {
    return unlink(joinFile(_fspath, filename).c_str()) == 0;
}

bool FileSystem::deleteRecursive(const String& path) {
    struct stat statbuf;

    if (stat(path.c_str(), &statbuf) != 0) {
        return false;
    }
    if (!S_ISDIR(statbuf.st_mode)) {
        return unlink(path.c_str()) == 0;
    }
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return false;
    }

    bool result = true;
    while (dir) {
        struct dirent* file = readdir(dir);
        if (!file) {
            break;
        }

        const char* name     = file->d_name;
        String      fullPath = path.endsWith("/") ? path + name : path + "/" + name;

        if (file->d_type == DT_DIR) {
            if (!deleteRecursive(fullPath)) {
                result = false;
            }
        } else {
            if (unlink(fullPath.c_str())) {
                result = false;
                break;
            }
        }
    }
    return result ? rmdir(path.c_str()) == 0 : false;
}

bool FileSystem::deleteDir() {
    return deleteRecursive(_fspath);
}
bool FileSystem::deleteDir(const String& filename) {
    return deleteRecursive(joinFile(_fspath, filename).c_str());
}
