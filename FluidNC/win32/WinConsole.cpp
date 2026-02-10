#include <conio.h>
#include <stdio.h>
#include <string>
#include <unistd.h>

#include "Serial.h"  // allChannels
#include "Driver/Console.h"
#include "WinConsole.h"

#include <windows.h>
#include <iostream>

#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
const int ENABLE_VIRTUAL_TERMINAL_PROCESSING = 0x0004;
const int ENABLE_VIRTUAL_TERMINAL_INPUT      = 0x0200;
#endif

static HANDLE hStdin;
static HANDLE hStdout;
static WORD   wOldColorAttrs;
static DWORD  fdwNewInMode;
static DWORD  fdwNewOutMode;
static DWORD  fdwOldInMode;
static DWORD  fdwOldOutMode;

extern void cleanupThreads();

static void editModeOn() {
    SetConsoleMode(hStdout, fdwOldOutMode);
    // SetConsoleMode(hStdin, fdwOldInMode | ENABLE_PROCESSED_INPUT | ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    // SetConsoleMode(hStdin, fdwOldInMode | ENABLE_LINE_INPUT);
    SetConsoleMode(hStdin, ENABLE_PROCESSED_INPUT | ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_QUICK_EDIT_MODE | ENABLE_EXTENDED_FLAGS);
}

static void editModeOff() {
    SetConsoleMode(hStdout, fdwNewOutMode);
    // SetConsoleMode(hStdin, fdwOldInMode | ENABLE_PROCESSED_INPUT | ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    // SetConsoleMode(hStdin, fdwOldInMode | ENABLE_LINE_INPUT);
    SetConsoleMode(hStdin, fdwNewInMode);
}

static void deinit() {
    editModeOn();
    SetConsoleMode(hStdout, fdwOldOutMode);
    SetConsoleMode(hStdin, fdwOldInMode);
    SetConsoleTextAttribute(hStdout, wOldColorAttrs);
    cleanupThreads();
}

static void errorExit(const char* msg) {
    std::cerr << msg << std::endl;
    std::cerr << "..press any key to continue" << std::endl;
    getch();

    // Restore input mode on exit.
    deinit();
    exit(1);
}

static void okayExit(const char* msg) {
    // Restore input mode on exit.
    std::cout << msg << std::endl;
    deinit();
    exit(0);
}

static void clearScreen() {
    std::cout << "\x1b[2J";
}

WinConsole::WinConsole(bool addCR) : Channel("WindowsConsole", addCR) {}

size_t WinConsole::write(uint8_t ch) {
    putchar(char(ch));
    return 1;
}

// Stream methods (Channel inherits from Stream)
int WinConsole::available(void) {
    return kbhit() ? 1 : 0;
}
int WinConsole::read() {
    int n;
    if ((n = available()) < 1) {
        return -1;
    }

    char c;
    ::ReadFile(hStdin, &c, 1, NULL, NULL);

#define CTRL(N) ((N)&0x1f)
    switch (c) {
        case CTRL(']'):
            okayExit("Exited by ^]");
        case CTRL('W'):
            clearScreen();
            break;
        case CTRL('Q'):
            okayExit("Exited by ^Q");
        case CTRL('C'):
            okayExit("Exited by ^C");
        default:
            return c;
    }
    return -1;
}

void WinConsole::init() {
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    if (hStdin == INVALID_HANDLE_VALUE) {
        errorExit("Can't get stdin handle");
    }

    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdout == INVALID_HANDLE_VALUE) {
        errorExit("Can't get stdout handle");
    }

    if (!GetConsoleMode(hStdout, &fdwOldOutMode)) {
        errorExit("Can't get stdin mode");
    }

    if (!GetConsoleMode(hStdin, &fdwOldInMode)) {
        errorExit("Can't get stdout mode");
    }

    CONSOLE_SCREEN_BUFFER_INFO csbiInfo;
    GetConsoleScreenBufferInfo(hStdout, &csbiInfo);
    wOldColorAttrs = csbiInfo.wAttributes;
    SetConsoleTextAttribute(hStdout, 0x0f);

    // Enable handling of escape sequences on output
    fdwNewOutMode = ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_WRAP_AT_EOL_OUTPUT;
    if (!SetConsoleMode(hStdout, fdwNewOutMode)) {
        errorExit("setConsoleColor failed");
    }

    // Enable conversion of special keys to escape sequences
    fdwNewInMode = ENABLE_VIRTUAL_TERMINAL_INPUT;
    if (!SetConsoleMode(hStdin, fdwNewInMode)) {
        errorExit("setConsoleMode failed");
    }

    editModeOff();

    _lineedit = new Lineedit(this, _line, Channel::maxLine - 1);
    allChannels.registration(this);
    log_info("WinConsole created");
}

int WinConsole::rx_buffer_available() {
    return 128 - available();
}

bool WinConsole::realtimeOkay(char c) {
    return _lineedit->realtime(c);
}

bool WinConsole::lineComplete(char* line, char c) {
    if (_lineedit->step(c)) {
        _linelen        = _lineedit->finish();
        _line[_linelen] = '\0';
        strcpy(line, _line);
        _linelen = 0;
        return true;
    }
    return false;
}

WinConsole winConsole(true);
Channel&   Console = winConsole;
