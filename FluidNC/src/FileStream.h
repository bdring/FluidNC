#pragma once

#include <Stream.h>

extern "C" {
#include <stdio.h>
}

class FileStream : public Stream {
    bool   _isSD;
    FILE*  _fd;
    String _path;

public:
    FileStream(const char* filename, const char* mode, const char* defaultFs = "localfs");

    String path() {
        String retval = _path;
        retval.replace("/spiffs/", "/localfs/");
        return retval;
    }
    int    available() override;
    int    read() override;
    int    peek() override;
    void   flush() override;
    size_t readBytes(char* buffer, size_t length) override;  // read chars from stream into buffer

    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buffer, size_t length) override;

    ~FileStream();
};
