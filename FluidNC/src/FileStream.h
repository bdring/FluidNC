// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// FileStream inherits from Channel, making it possible to use a
// file on either SD or the local FLASH filesystem as a source
// or sink for data that would otherwise be sent over a Channel.
// That is useful for things like logging to a file or transferring
// data between files and other channels.
// The methods are the same as for the Channel class.

#pragma once

#include "Channel.h"
#include "FluidPath.h"

extern "C" {
#include <stdio.h>
}

class FileStream : public Channel {
    FluidPath _fpath;  // Keeps the volume mounted while the file is in use
    FILE*     _fd;
    size_t    _size;

    void setup(const char* mode);

public:
    FileStream() = default;
    FileStream(String filename, const char* mode, const char* defaultFs = "") : FileStream(filename.c_str(), mode, defaultFs) {}
    FileStream(const char* filename, const char* mode, const char* defaultFs = "");
    FileStream(FluidPath fpath, const char* mode);

    FluidPath fpath() { return _fpath; }

    String path();
    String name();
    int    available() override;
    int    read() override;
    int    peek() override;
    void   flush() override;

    size_t readBytes(char* buffer, size_t length) { return read((uint8_t*)buffer, length); }

    size_t read(char* buffer, size_t length);  // read chars from stream into buffer
    size_t read(uint8_t* buffer, size_t length) { return read((char*)buffer, length); }

    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buffer, size_t length) override;

    size_t size();
    size_t position();

    // pollLine() is a required method of the Channel class that
    // FileStream implements as a no-op.
    Channel* pollLine(char* line) override { return nullptr; }

    ~FileStream();
};
