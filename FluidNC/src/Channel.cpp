// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Channel.h"
#include "Machine/MachineConfig.h"  // config
#include "Serial.h"                 // execute_realtime_command

Channel* Channel::pollLine(char* line) {
    handle();
    while (1) {
        int ch = read();
        if (ch < 0) {
            break;
        }
        if (is_realtime_command(ch)) {
            execute_realtime_command(static_cast<Cmd>(ch), *this);
            continue;
        }
        if (!line) {
            continue;
        }
        // The objective here is to treat any of CR, LF, or CR-LF
        // as a single line ending.  When we see CR, we immediately
        // complete the line, setting a flag to say that the last
        // character was CR.  When we see LF, if the last character
        // was CR, we ignore the LF because the line has already
        // been completed, otherwise we complete the line.
        if (ch == '\n') {
            if (_lastWasCR) {
                _lastWasCR = false;
                continue;
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
            return this;
        }
        _lastWasCR = ch == '\r';
        if (_lastWasCR) {
            // Return the complete line
            _line[_linelen] = '\0';
            strcpy(line, _line);
            _linelen = 0;
            return this;
        }
        if (ch == '\b') {
            // Simple editing for interactive input - backspace erases
            if (_linelen) {
                --_linelen;
            }
            continue;
        }
        if (_linelen < (Channel::maxLine - 1)) {
            _line[_linelen++] = ch;
        } else {
            //  report_status_message(Error::Overflow, this);
            // _linelen = 0;
            // Probably should discard the rest of the line too.
            // _discarding = true;
        }
    }
    return nullptr;
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
