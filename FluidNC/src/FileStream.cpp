// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "FileStream.h"
#include "Machine/MachineConfig.h"  // config->
#include "Driver/localfs.h"
#include "./Maslow/Maslow.h"

std::string FileStream::path() {
    return _fpath.c_str();
}

std::string FileStream::name() {
    return path();
}

int FileStream::available() {
    return size() - position();
}

int FileStream::read() {
    Maslow.readingFromSD = true;
    char   data;
    size_t res = fread(&data, 1, 1, _fd);
    Maslow.readingFromSD = false;
    return res == 1 ? data : -1;
}

int FileStream::peek() {
    return -1;
}

void FileStream::flush() {}

size_t FileStream::read(char* buffer, size_t length) {
    Maslow.readingFromSD = true;
    size_t res = fread(buffer, 1, length, _fd);
    Maslow.readingFromSD = false;
    return res;
}

size_t FileStream::write(uint8_t c) {
    Maslow.readingFromSD = true;
    size_t res = FileStream::write(&c, 1);
    Maslow.readingFromSD = false;
    return res;
}

size_t FileStream::write(const uint8_t* buffer, size_t length) {
    Maslow.readingFromSD = true;
    size_t res = fwrite(buffer, 1, length, _fd);
    Maslow.readingFromSD = false;
    return res;
}

size_t FileStream::size() {
    return _size;
}

size_t FileStream::position() {
    return ftell(_fd);
}

void FileStream::setup(const char* mode) {
    _fd = fopen(_fpath.c_str(), mode);

    if (!_fd) {
        bool opening = strcmp(mode, "w");
        log_verbose("Cannot " << (opening ? "open" : "create") << " file " << _fpath.c_str());
        throw opening ? Error::FsFailedOpenFile : Error::FsFailedCreateFile;
    }
    _size = stdfs::file_size(_fpath);
}

FileStream::FileStream(const char* filename, const char* mode, const char* fs) : Channel("file"), _fpath(filename, fs) {
    setup(mode);
}

FileStream::FileStream(FluidPath fpath, const char* mode) : Channel("file") {
    std::swap(_fpath, fpath);
    setup(mode);
}

FileStream::~FileStream() {
    fclose(_fd);
}
