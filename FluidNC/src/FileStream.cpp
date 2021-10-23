#include "FileStream.h"
#include "Machine/MachineConfig.h"  // config->
#include "SDCard.h"

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

FileStream::FileStream(const char* filename, const char* mode, const char* defaultFs) {
    if (!filename || !*filename) {
        throw Error::FsFailedCreateFile;
    }

    // Insert the default file system prefix if a file system name is not present
    if (*filename != '/') {
        _path = "/";
        _path += defaultFs;
        _path += "/";
    }

    _path += filename;

    // Map /localfs/ to the actual name of the local file system
    if (_path.startsWith("/localfs/")) {
        _path.replace("/localfs/", "/spiffs/");
    }
    if (_path.startsWith("/sd/")) {
        if (config->_sdCard->begin(SDCard::State::BusyWriting) != SDCard::State::Idle) {
            throw Error::FsFailedMount;
        }
        _isSD = true;
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
