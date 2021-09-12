#pragma once

#include <Print.h>

extern "C" {
#include <stdio.h>
}

class FileStream : public Print {
    bool  _isSD;
    FILE* _fd;

public:
    FileStream(const char* filename, const char* defaultFs);

    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buffer, size_t length) override;
    ~FileStream();
};
