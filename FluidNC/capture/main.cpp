extern "C" void setup();
extern "C" void loop();

#include "Platform.h"

#include <string>
// Signal handling for SIGINT
#include <csignal>
// Forward declaration for restart
void restart();
#include <atomic>
#include "SignalHandler.h"

#if 0
// Global StringChannel pointer - used by posix/Console.cpp
#    include "StringChannel.h"
StringChannel* g_stringChannel = nullptr;
#endif

std::string command_line_cmds;
bool        continue_after_cmds = false;

int main(int argc, char** argv) {
    // Parse command line arguments looking for -c flags
    // Concatenate all -c arguments into a single string with newline terminators
    // Usage: ./program -c "G0 X10" -c "M5" or ./program -c '$sd/run=file.nc'
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "-") {
            continue_after_cmds = true;
        }
        if (std::string(argv[i]) == "-c" && i + 1 < argc) {
            command_line_cmds += argv[++i];
            command_line_cmds += '\n';
        }
    }

    // Install SIGINT handler to set a flag (async-signal-safe)
    static volatile std::sig_atomic_t g_sigint_received = 0;
    SignalHandler::registerHandler(SIGINT, [](int) { g_sigint_received = 1; });

    setup();
    while (!should_exit()) {
        if (g_sigint_received) {
            restart();
        }
        loop();
    }
    return 0;
}
