// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Channel.h"
#include "Machine/MachineConfig.h"  // config
#include "Serial.h"                 // execute_realtime_command

Channel* Channel::pollLine(char* line) {
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
        if (ch == '\r') {
            // Ignore CR
            continue;
        }
        if (ch == '\b') {
            // Simple editing for interactive input - backspace erases
            if (_linelen) {
                --_linelen;
            }
            continue;
        }
        if (ch == '\n') {
            // if (_discarding) {
            //     _linelen = 0;
            //     _discarding = false;
            //     return nullptr;
            // }
            _line[_linelen] = '\0';
            strcpy(line, _line);
            _linelen = 0;
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
