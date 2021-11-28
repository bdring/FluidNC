// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "FileStream.h"
#include "Machine/MachineConfig.h"  // config->
#include "SDCard.h"
#include "Logging.h"
#include <sys/stat.h>

int FileStream::available() {
    return 1;
}
int FileStream::read() {
    char   data;
    size_t res = fread(&data, 1, 1, _fd);
    return res == 1 ? data : -1;
}
int FileStream::peek() {
    return -1;
}
void FileStream::flush() {}

size_t FileStream::readBytes(char* buffer, size_t length) {
    return fread(buffer, 1, length, _fd);
}

size_t FileStream::write(uint8_t c) {
    return FileStream::write(&c, 1);
}

size_t FileStream::write(const uint8_t* buffer, size_t length) {
    return fwrite(buffer, 1, length, _fd);
}

size_t FileStream::size() {
    struct stat statbuf;
    return stat(_path.c_str(), &statbuf) ? 0 : statbuf.st_size;
}

size_t FileStream::position() {
    fpos_t pos;
    return fgetpos(_fd, &pos) ? 0 : pos;
}

FileStream::FileStream(String filename, const char* mode, const char* defaultFs) : FileStream(filename.c_str(), mode, defaultFs) {}

FileStream::FileStream(const char* filename, const char* mode, const char* defaultFs) : Channel("file") {
    const char* actualLocalFs = "/spiffs/";
    const char* sdPrefix      = "/sd/";
    const char* localFsPrefix = "/localfs/";

    if (!filename || !*filename) {
        throw Error::FsFailedCreateFile;
    }
    _path = filename;

    // Map file system names to canonical form
    if (_path.startsWith("/SD/")) {
        _path.replace("/SD/", sdPrefix);
    } else if (_path.startsWith(sdPrefix)) {
        // Leave path as-is
    } else if (_path.startsWith(localFsPrefix)) {
        _path.replace(localFsPrefix, actualLocalFs);
    } else if (_path.startsWith("/LOCALFS/")) {
        _path.replace("/LOCALFS/", actualLocalFs);
    } else {
        if (*filename != '/') {
            _path = '/' + _path;
        }
        // _path now begins with /
        if (!strcmp(defaultFs, "/localfs")) {
            // If the default filesystem is /localfs, replace the initial /
            // with, for example, "/spiffs/", instead of the surrogate
            _path.replace("/", actualLocalFs);
        } else {
            // If the default filesystem is not /localfs, insert
            // the defaultFs name - which does not end with / -
            // at the beginning of _path, which does begin with /
            _path = defaultFs + _path;
        }
    }
    if (_path.startsWith(sdPrefix)) {
        if (config->_sdCard->begin(SDCard::State::BusyWriting) != SDCard::State::Idle) {
            log_info("FS busy ");
            throw Error::FsFailedMount;
        }
        _isSD = true;
    }
    if (_path.startsWith(actualLocalFs) && _path.length() > (30 + strlen(actualLocalFs))) {
        log_info("Filename too long");
    }
    _fd = fopen(_path.c_str(), mode);
    if (!_fd) {
        throw strcmp(mode, "w") ? Error::FsFailedOpenFile : Error::FsFailedCreateFile;
    }
}

FileStream::~FileStream() {
    fclose(_fd);
    if (_isSD) {
        config->_sdCard->end();
    }
}
