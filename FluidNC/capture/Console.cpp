#include <windows.h>
#include "Console.h"
#include <iostream>

#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
const int ENABLE_VIRTUAL_TERMINAL_PROCESSING = 0x0004;
const int ENABLE_VIRTUAL_TERMINAL_INPUT      = 0x0200;
#endif

HANDLE hStdin;
HANDLE hStdout;
WORD   wOldColorAttrs;
DWORD  fdwNewInMode;
DWORD  fdwNewOutMode;
DWORD  fdwOldInMode;
DWORD  fdwOldOutMode;

bool initConsole() {
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    if (hStdin == INVALID_HANDLE_VALUE) {
        return false;
    }

    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdout == INVALID_HANDLE_VALUE) {
        return false;
    }

    if (!GetConsoleMode(hStdout, &fdwOldOutMode)) {
        return false;
    }

    if (!GetConsoleMode(hStdin, &fdwOldInMode)) {
        return false;
    }

    return true;
}

void editModeOn() {
    SetConsoleMode(hStdout, fdwOldOutMode);
    // SetConsoleMode(hStdin, fdwOldInMode | ENABLE_PROCESSED_INPUT | ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    // SetConsoleMode(hStdin, fdwOldInMode | ENABLE_LINE_INPUT);
    SetConsoleMode(hStdin, ENABLE_PROCESSED_INPUT | ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_QUICK_EDIT_MODE | ENABLE_EXTENDED_FLAGS);
}

void editModeOff() {
    SetConsoleMode(hStdout, fdwNewOutMode);
    // SetConsoleMode(hStdin, fdwOldInMode | ENABLE_PROCESSED_INPUT | ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    // SetConsoleMode(hStdin, fdwOldInMode | ENABLE_LINE_INPUT);
    SetConsoleMode(hStdin, fdwNewInMode);
}

bool setConsoleModes() {
    // Enable conversion of special keys to escape sequences
    fdwNewInMode = ENABLE_VIRTUAL_TERMINAL_INPUT;
    if (!SetConsoleMode(hStdin, fdwNewInMode)) {
        return false;
    }
    return true;
}

void clearScreen() {
    std::cout << "\x1b[2J";
}

bool setConsoleColor() {
    CONSOLE_SCREEN_BUFFER_INFO csbiInfo;
    GetConsoleScreenBufferInfo(hStdout, &csbiInfo);
    wOldColorAttrs = csbiInfo.wAttributes;
    SetConsoleTextAttribute(hStdout, 0x0f);

    // Enable handling of escape sequences on output
    fdwNewOutMode = ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_WRAP_AT_EOL_OUTPUT;
    if (!SetConsoleMode(hStdout, fdwNewOutMode)) {
        return false;
    }

    return true;
}

void restoreConsoleModes() {
    SetConsoleMode(hStdout, fdwOldOutMode);
    SetConsoleMode(hStdin, fdwOldInMode);
    SetConsoleTextAttribute(hStdout, wOldColorAttrs);
}

int getConsoleChar() {
    char c;
    return ::ReadFile(hStdin, &c, 1, NULL, NULL) ? c : -1;
}

bool availConsoleChar() {
    INPUT_RECORD r;
    DWORD        n;
    PeekConsoleInput(hStdin, &r, 1, &n);

    return n > 0;
}
