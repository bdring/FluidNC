#include "StartupLog.h"
#include <sstream>
#include "Protocol.h"  // send_line()

size_t StartupLog::write(uint8_t data) {
    _messages += (char)data;
    return 1;
}
std::string StartupLog::messages() {
    return _messages;
}
void StartupLog::dump(Channel& out) {
    std::istringstream iss(_messages);
    for (std::string line; std::getline(iss, line);) {
        log_to(out, line);
    }
}

StartupLog::~StartupLog() {}

StartupLog startupLog("Startup Log");
