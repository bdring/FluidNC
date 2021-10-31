// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "WebUI/Authentication.h"
#include "Channel.h"
#include "Error.h"

#include <cstdint>

// Forward declaration:
namespace fs {
    class FS;
}

class InputFile {
public:
    class FileWrap;  // holds a single 'File'; we don't want to include <FS.h> here

private:
    FileWrap* _pImpl;  // this is actually a 'File'; we don't want to include <FS.h>

    Channel&                   _channel;
    WebUI::AuthenticationLevel _auth_level;

public:
    uint32_t _line_num;  // the most recent line number read

    InputFile(fs::FS& fs, const char* path, Channel& channel, WebUI::AuthenticationLevel auth_level);
    InputFile(const InputFile&) = delete;
    InputFile& operator=(const InputFile&) = delete;

    Error readLine(char* line, int len);
    float percent_complete();

    Channel&                   getChannel() { return _channel; }
    WebUI::AuthenticationLevel getAuthLevel() { return _auth_level; }

    const char* filename();

    ~InputFile();
};
extern InputFile* infile;
