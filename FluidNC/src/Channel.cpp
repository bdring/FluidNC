// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Channel.h"
#include "Machine/MachineConfig.h"  // config
#include "Serial.h"                 // execute_realtime_command

void Channel::flushRx() {
    _linelen   = 0;
    _lastWasCR = false;
    while (_queue.size()) {
        _queue.pop();
    }
}

int Channel::peek() {
    return _queue.size() ? _queue.front() : -1;
}

int Channel::read() {
    if (_queue.size()) {
        uint8_t c = _queue.front();
        _queue.pop();
        return c;
    }
    return -1;
}

bool Channel::lineComplete(char* line, char ch) {
    // The objective here is to treat any of CR, LF, or CR-LF
    // as a single line ending.  When we see CR, we immediately
    // complete the line, setting a flag to say that the last
    // character was CR.  When we see LF, if the last character
    // was CR, we ignore the LF because the line has already
    // been completed, otherwise we complete the line.
    if (ch == '\n') {
        if (_lastWasCR) {
            _lastWasCR = false;
            return false;
        }
        // if (_discarding) {
        //     _linelen = 0;
        //     _discarding = false;
        //     return nullptr;
        // }

        // Return the complete line
        _line[_linelen] = '\0';
        strcpy(line, _line);
        _linelen = 0;
        return true;
    }
    _lastWasCR = ch == '\r';
    if (_lastWasCR) {
        // Return the complete line
        _line[_linelen] = '\0';
        strcpy(line, _line);
        _linelen = 0;
        return true;
    }
    if (ch == '\b') {
        // Simple editing for interactive input - backspace erases
        if (_linelen) {
            --_linelen;
        }
        return false;
    }
    if (_linelen < (Channel::maxLine - 1)) {
        _line[_linelen++] = ch;
    } else {
        //  report_status_message(Error::Overflow, this);
        // _linelen = 0;
        // Probably should discard the rest of the line too.
        // _discarding = true;
    }
    return false;
}

Channel* Channel::pollLine(char* line) {
    handle();
    while (1) {
        int ch;
        if (line && _queue.size()) {
            ch = _queue.front();
            _queue.pop();
        } else {
            ch = read();
        }

        // ch will only be negative if read() was called and returned -1
        // The _queue path will return only nonnegative character values
        if (ch < 0) {
            break;
        }
        if (realtimeOkay(ch) && is_realtime_command(ch)) {
            execute_realtime_command(static_cast<Cmd>(ch), *this);
            continue;
        }
        if (!line) {
            // If we are not able to handle a line we save the character
            // until later
            _queue.push(uint8_t(ch));
            continue;
        }
        if (lineComplete(line, ch)) {
            return this;
        }
    }
    return nullptr;
}

int Channel::available() {
    return _queue.size();
}

void Channel::ack(Error status) {
    switch (status) {
        case Error::Ok:  // Error::Ok
            print("ok\n");
            break;
        default:
            // With verbose errors, the message text is displayed instead of the number.
            // Grbl 0.9 used to display the text, while Grbl 1.1 switched to the number.
            // Many senders support both formats.
            print("error:");
            if (config->_verboseErrors) {
                print(errorString(status));
            } else {
                print(static_cast<int>(status));
            }
            write('\n');
            break;
    }
}
