// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "InputFile.h"

#include "Report.h"
#include "Logging.h"

InputFile::InputFile(const FileSystem::FsInfo& fs, const char* path, WebUI::AuthenticationLevel auth_level, Channel& out) :
    FileStream(path, "r", fs), _auth_level(auth_level), _out(out), _line_num(0) {}
/*
  Read a line from the file
  Returns Error::Ok if a line was read, even if the line was empty.
  Returns Error::EOF on end of file.
  Returns other Error code on error, after displaying a message.
*/
Error InputFile::readLine(char* line, int maxlen) {
    ++_line_num;
    int len = 0;
    int c;
    while ((c = read()) >= 0) {
        if (len >= maxlen) {
            return Error::LineLengthExceeded;
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            break;
        }
        line[len++] = c;
    }
    line[len] = '\0';
    return len || c >= 0 ? Error::Ok : Error::Eof;
}

// return a percentage complete 50.5 = 50.5%
float InputFile::percent_complete() {
    return (float)position() / (float)size() * 100.0f;
}

InputFile::~InputFile() {}

InputFile* infile = nullptr;
