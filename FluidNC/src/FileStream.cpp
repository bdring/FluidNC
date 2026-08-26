// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "FileStream.h"
#include "Machine/MachineConfig.h"  // config->

std::string FileStream::path() {
    return _fpath.string();
}

std::string FileStream::name() {
    return path();
}

int FileStream::available() {
    return size() - position();
}

int FileStream::read() {
    if (!_fd) {
        return -1;
    }
    char   data;
    size_t res = fread(&data, 1, 1, _fd);
    return res == 1 ? data : -1;
}

bool FileStream::read_failed() {
    return _io_error || (_fd && ferror(_fd) != 0);
}

int FileStream::peek() {
    return -1;
}

void FileStream::flush() {}

int FileStream::read(char* buffer, size_t length) {
    if (!_fd) {
        return -1;
    }
    size_t got = fread(buffer, 1, length, _fd);
    // Report the failure once the buffered data has been handed back, so
    // callers that check for a negative return actually see one.  Previously
    // this could only ever return >= 0, which made those checks dead code.
    if (got == 0 && read_failed()) {
        return -1;
    }
    return static_cast<int>(got);
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
    // While saved, the file is closed and _saved_position is where we left off.
    return _fd ? ftell(_fd) : _saved_position;
}

void FileStream::setup(const char* mode) {
    _fd = fopen(_fpath.string().c_str(), mode);

    if (!_fd) {
        bool opening = strcmp(mode, "w");
        throw ErrorException(opening ? Error::FsFailedOpenFile : Error::FsFailedCreateFile);
    }
    _size = stdfs::file_size(_fpath);
}

FileStream::FileStream(const char* filename, const char* mode, const Volume& fs) : Channel(filename), _fpath(filename, fs), _mode(mode) {
    setup(mode);
}

FileStream::FileStream(FluidPath fpath, const char* mode) : Channel("file"), _mode(mode) {
    std::swap(_fpath, fpath);
    setup(mode);
}

void FileStream::set_position(size_t pos) {
    fseek(_fd, pos, SEEK_SET);
}

void FileStream::save() {
    _saved_position = position();
    fclose(_fd);
    _fd = nullptr;
}

void FileStream::restore() {
    _fd = fopen(_fpath.string().c_str(), _mode);
    if (_fd) {
        fseek(_fd, _saved_position, SEEK_SET);
    } else {
        // The file could not be reopened - the SD card was pulled, or the
        // mount dropped.  Record it so the next read reports an error.  It
        // used to just leave _fd null, and a read from a null FILE* reports
        // end of file, which the job machinery reads as "program complete":
        // the job would stop partway through and claim it had finished.
        _io_error = true;
        log_error("Cannot reopen " << _fpath.string() << "; the job will be stopped");
    }
}

FileStream::~FileStream() {
    if (_fd) {
        fclose(_fd);
    }
}
