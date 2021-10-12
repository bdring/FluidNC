#include "FileStream.h"
#include "Machine/MachineConfig.h"  // config->
#include "SDCard.h"

size_t FileStream::write(uint8_t c) {
    return FileStream::write(&c, 1);
}

size_t FileStream::write(const uint8_t* buffer, size_t length) {
    return fwrite(buffer, 1, length, _fd);
}

FileStream::FileStream(const char* filename, const char* defaultFs) {
    String path;

    if (!filename || !*filename) {
        throw Error::FsFailedCreateFile;
    }

    // Insert the default file system prefix if a file system name is not present
    if (*filename != '/') {
        path = "/";
        path += defaultFs;
        path += "/";
    }

    path += filename;

    // Map /localfs/ to the actual name of the local file system
    if (path.startsWith("/localfs/")) {
        path.replace("/localfs/", "/spiffs/");
    }
    if (path.startsWith("/sd/")) {
        if (config->_sdCard->begin(SDCard::State::BusyWriting) != SDCard::State::Idle) {
            throw Error::FsFailedMount;
        }
        _isSD = true;
    }

    _fd = fopen(path.c_str(), "w");
    if (!_fd) {
        throw Error::FsFailedCreateFile;
    }
}

FileStream::~FileStream() {
    fclose(_fd);
    if (_isSD) {
        config->_sdCard->end();
    }
}
