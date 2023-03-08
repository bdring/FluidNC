// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// InputFile is used for executing and displaying GCode from a file.
// The file can be located on any supported file system, such as SD card or the local file system.
// InputFile inherits from FileStream, adding the following features:
//  - Reads lines delimited by newline
//  - For reporting the progress of GCode execution, counts the number of lines read and
//    the percentage of the file size that has currently been read.
//  - For reporting status, remembers the I/O channel that started the process of using the file.
// FileStream's Channel member is not that same Channel that FileStream ultimately
// inherits from; rather it is a separate channel that is use for status reporting.

#pragma once

#include "WebUI/Authentication.h"
#include "FileStream.h"  // FileStream and Channel
#include "Error.h"

#include <cstdint>

class InputFile : public FileStream {
private:
    WebUI::AuthenticationLevel _auth_level;

    // The channel that triggered the use of this file, through which
    // status about the use of this file will be reported.
    Channel& _out;

    uint32_t _line_num;  // the most recent line number read
    bool     _readyNext = true;

public:
    static std::string _progress;

    // fsname is the default file system on which the file is located, in case the path does not specify
    // path is the full path to the file
    // channel is the I/O channel on which status about the use of this file will be reported
    InputFile(const char* fsname, const char* path, WebUI::AuthenticationLevel auth_level, Channel& channel);

    InputFile(const InputFile&) = delete;
    InputFile& operator=(const InputFile&) = delete;

    // readLine() differs from pollLine() in the Channel API as follows:

    // pollLine() is used with character-oriented input Channels whose
    // data is provided by an external source whose timing is unknown.
    // You might get a character now or you might get one sometime
    // in the indefinite future, or perhaps never.

    // readLine() is used with file storage devices.  When you ask for
    // data, you either get it "immediately" or you get a response
    // saying you will never get it (error or end-of-file).

    Error readLine(char* line, int len);

    // These are used for feedback about the progress of the operation
    uint32_t getLineNumber() { return _line_num; }
    float    percent_complete();

    // This tells where to send the feedback
    Channel& getChannel() { return _out; }

    WebUI::AuthenticationLevel getAuthLevel() { return _auth_level; }

    // Channel methods
    size_t   write(uint8_t c) override { return 0; }
    void     ack(Error status) override;
    Channel* pollLine(char* line) override;
    void     stopJob() override;

    ~InputFile();
};
