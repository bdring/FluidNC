#include <iostream>
#include <thread>

#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>

// termios defines baud-rate macros like B0 and B9600 that collide with
// Arduino-Emulator's Binary.h enum constants on Linux.
#ifdef B0
#    undef B0
#endif
#ifdef B50
#    undef B50
#endif
#ifdef B75
#    undef B75
#endif
#ifdef B110
#    undef B110
#endif
#ifdef B134
#    undef B134
#endif
#ifdef B150
#    undef B150
#endif
#ifdef B200
#    undef B200
#endif
#ifdef B300
#    undef B300
#endif
#ifdef B600
#    undef B600
#endif
#ifdef B1200
#    undef B1200
#endif
#ifdef B1800
#    undef B1800
#endif
#ifdef B2400
#    undef B2400
#endif
#ifdef B4800
#    undef B4800
#endif
#ifdef B9600
#    undef B9600
#endif
#ifdef B19200
#    undef B19200
#endif
#ifdef B38400
#    undef B38400
#endif
#ifdef B57600
#    undef B57600
#endif
#ifdef B115200
#    undef B115200
#endif
#ifdef B230400
#    undef B230400
#endif
#ifdef B460800
#    undef B460800
#endif
#ifdef B500000
#    undef B500000
#endif
#ifdef B576000
#    undef B576000
#endif
#ifdef B921600
#    undef B921600
#endif
#ifdef B1000000
#    undef B1000000
#endif
#ifdef B1152000
#    undef B1152000
#endif
#ifdef B1500000
#    undef B1500000
#endif
#ifdef B2000000
#    undef B2000000
#endif
#ifdef B2500000
#    undef B2500000
#endif
#ifdef B3000000
#    undef B3000000
#endif
#ifdef B3500000
#    undef B3500000
#endif
#ifdef B4000000
#    undef B4000000
#endif

#include "Serial.h"  // allChannels
#include "Driver/Console.h"

#include "Channel.h"
#include "lineedit.h"
#include "StringChannel.h"
#include "../capture/freertos/task.h"
//#include "SimulatorWebSocketServer.h"

// Global StringChannel pointer - set by main() if command-line string injection is requested
// extern Channel* g_stringChannel;
extern std::string command_line_cmds;
extern "C" {
extern bool continue_after_cmds;
};

static struct termios _orig_termios;

class PosixConsole : public Channel {
private:
    Lineedit* _lineedit;
    bool      _exit_after_cmds = false;

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
        if (command_line_cmds.size()) {
            push(command_line_cmds);
            _exit_after_cmds = !continue_after_cmds;
        }

        editModeOff();
        _lineedit = new Lineedit(this, _line, Channel::maxLine - 1);
        allChannels.registration(this);

#if 0
        // Register StringChannel subordinate object if provided (following esp32s3 Console pattern)
        if (g_stringChannel) {
            g_stringChannel->init();
        }
#endif

        // Start WebSocket server
        //fprintf(stderr, "[Console::init] Starting WebSocket server...\n");
        //SimulatorWS::SimulatorWebSocketServer::instance().init(9000);
        //fprintf(stderr, "[Console::init] WebSocket server init returned\n");
    };

    // This is a no-op so the initial call to it does not clear the queue
    void flushRx() override {}

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
    Error pollLine(char* line) override {
        if (line && _queue.empty() && _exit_after_cmds) {
            cleanup_threads();
            exit(0);
        }
        return Channel::pollLine(line);
    }
};

PosixConsole posixConsole(true);
Channel&     Console = posixConsole;
