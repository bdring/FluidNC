#include "Channel.h"
#include "Serial.h"  // execute_realtime_command

Channel* Channel::pollLine(char* line) {
    int ch = read();
    if (ch < 0 || ch == '\r') {
        return nullptr;
    }
    if (is_realtime_command(ch)) {
        execute_realtime_command(static_cast<Cmd>(ch), *this);
        return nullptr;
    }
    if (!line) {
        return nullptr;
    }
    if (ch == '\b') {
        // Simple editing for interactive input - backspace erases
        if (_linelen) {
            --_linelen;
        }
        return nullptr;
    }
    if (ch == '\n') {
        // if (_discarding) {
        //     _linelen = 0;
        //     ++_line_num;
        //     _discarding = false;
        //     return nullptr;
        // }
        _line[_linelen] = '\0';
        strcpy(line, _line);
        _linelen = 0;
        ++_line_num;
        return this;
    }
    if (_linelen < (Channel::maxLine - 1)) {
        _line[_linelen++] = ch;
    } else {
        //  report_status_message(Error::Overflow, this);
        // _linelen = 0;
        // Probably should discard the rest of the line too.
        // _discarding = true;
    }
    return nullptr;
}
