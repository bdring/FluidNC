#include <iostream>
// #include <fstream>
#include <conio.h>
#include <stdio.h>
#include <string>
#include <Console.h>
#include <unistd.h>

extern void setup();
extern void loop();

extern void cleanupThreads();
static void errorExit(const char* msg) {
    std::cerr << msg << std::endl;
    std::cerr << "..press any key to continue" << std::endl;
    getch();

    // Restore input mode on exit.
    restoreConsoleModes();
    cleanupThreads();
    exit(1);
}

static void okayExit(const char* msg) {
    // Restore input mode on exit.
    std::cout << msg << std::endl;
    restoreConsoleModes();
    cleanupThreads();
    exit(0);
}
int inchar() {
    if (!availConsoleChar()) {
        return -1;
    }
    int c = getConsoleChar();

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

int main() {
    if (!initConsole()) {
        errorExit("Can't get console handles");
    }
    // editModeOn();
    editModeOff();
    if (!setConsoleColor()) {
        errorExit("setConsoleColor failed");
    }

    if (!setConsoleModes()) {
        errorExit("setConsoleModes failed");
    }
    setup();
    while (1) {
        loop();
    }
    okayExit("");
    return 0;
}
