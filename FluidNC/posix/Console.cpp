#include <iostream>

#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include "Serial.h"  // allChannels
#include "Driver/Console.h"

#include "Channel.h"
#include "lineedit.h"

static struct termios _orig_termios;

class PosixConsole : public Channel {
private:
    Lineedit* _lineedit;

public:
    PosixConsole(bool addCR = false) : Channel("PosixConsole", addCR) {}

    static void editModeOn() {
        // stdout changes?
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &_orig_termios);
    }

    static void editModeOff() {
        tcgetattr(STDIN_FILENO, &_orig_termios);
        struct termios raw = _orig_termios;

        // input modes: no break, no CR to NL, no parity check, no strip char,
        // no start/stop output control.
        raw.c_iflag &= ~(BRKINT | /* ICRNL | */ INPCK | ISTRIP | IXON);

        // output modes - disable post processing */
        //    raw.c_oflag &= ~(OPOST);
        raw.c_oflag |= ONLCR;

        // control modes - set 8 bit chars */
        raw.c_cflag |= (CS8);

        // local modes - choing off, canonical off, no extended functions,
        // no signal chars (^Z,^C) */
        raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_lflag |= ISIG;

        // control chars - set return condition: min number of bytes and timer.
        // We want read to return every single byte, without timeout. */
        raw.c_cc[VINTR] = 3; /* Ctrl-C interrupts process */
        // raw.c_cc[VMIN]  = 1;
        // raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

        // // The cfmakeraw function is a standard way to achieve raw mode
        // cfmakeraw(&raw);

        // // Additional configuration for read() behavior (VMIN and VTIME)
        // // VMIN=1, VTIME=0 means read() returns as soon as 1 character is available, blocking indefinitely until then
        // raw.c_cc[VMIN]  = 1;
        // raw.c_cc[VTIME] = 0;

        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

        atexit(editModeOn);  // Ensure terminal mode is restored on exit
    }

    void init() override {
        editModeOff();
        _lineedit = new Lineedit(this, _line, Channel::maxLine - 1);
        allChannels.registration(this);
    };

    // Print methods (Stream inherits from Print)
    size_t write(uint8_t c) override {
        //        fflush(STDOUT);
        return ::write(STDOUT_FILENO, &c, 1);
    }

    // Stream methods (Channel inherits from Stream)

    int available(void) override {
        int n;
        return ioctl(STDIN_FILENO, FIONREAD, &n) ? 0 : n;
    }

    int read() override {
        int n;
        if ((n = available()) < 1) {
            return -1;
        }

        char c;
        auto ret = ::read(STDIN_FILENO, &c, 1);
        return ret == 1 ? c : -1;
    }

    // Channel methods
    int rx_buffer_available() override { return 128 - available(); }

    bool realtimeOkay(char c) override { return _lineedit->realtime(c); }

    bool lineComplete(char* line, char c) override {
        if (_lineedit->step(c)) {
            _linelen        = _lineedit->finish();
            _line[_linelen] = '\0';
            strcpy(line, _line);
            _linelen = 0;
            return true;
        }
        return false;
    }
};

PosixConsole posixConsole(true);
Channel&     Console = posixConsole;
