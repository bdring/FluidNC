#include "StartupLog.h"

size_t StartupLog::write(uint8_t data) {
    _messages += (char)data;
    return 1;
}
String StartupLog::messages() {
    return _messages;
}
StartupLog::~StartupLog() {}

StartupLog startupLog("Startup Log");
