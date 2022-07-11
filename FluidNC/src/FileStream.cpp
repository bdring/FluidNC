// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "FileStream.h"
#include "Machine/MachineConfig.h"  // config->
#include "Logging.h"
#include <sys/stat.h>
#include <errno.h>

String FileStream::path() {
    return _fs->path();
}
String FileStream::name() {
    return path();
}

int FileStream::available() {
    return size() - position();
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

size_t FileStream::read(char* buffer, size_t length) {
    return fread(buffer, 1, length, _fd);
}

size_t FileStream::write(uint8_t c) {
    return FileStream::write(&c, 1);
}

size_t FileStream::write(const uint8_t* buffer, size_t length) {
    return fwrite(buffer, 1, length, _fd);
}

size_t FileStream::size() {
    return _size;
}

size_t FileStream::position() {
    return ftell(_fd);
}

FileStream::FileStream(const String& filename, const char* mode, const FileSystem::FsInfo& fs) : Channel("file") {
    if (filename == "") {
        throw Error::FsFailedCreateFile;
    }
    _fs = new FileSystem(filename, fs);

    _fd = fopen(_fs->path().c_str(), mode);
    if (_fd) {
        struct stat statbuf;
        stat(_fs->path().c_str(), &statbuf);
        _size = statbuf.st_size;
    } else {
        bool opening = strcmp(mode, "w");
        log_verbose("Cannot " << (opening ? "open" : "create") << " file " << _fs->path());
        delete _fs;
        throw opening ? Error::FsFailedOpenFile : Error::FsFailedCreateFile;
    }
}

FileStream::~FileStream() {
    fclose(_fd);
    delete _fs;
}
