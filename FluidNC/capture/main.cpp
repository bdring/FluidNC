extern void setup();
extern void loop();

// #include "StringChannel.h"
#include <string>

// Global StringChannel pointer - used by posix/Console.cpp
// StringChannel* g_stringChannel = nullptr;

std::string command_line_cmds;
extern "C" {
bool continue_after_cmds = false;
};

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

#if 0
    if (!combined_command.empty()) {
        g_stringChannel = new StringChannel(combined_command);
    }
#endif

    ::printf("command_line_cmds %s\n", command_line_cmds.c_str());
    setup();
    while (1) {
        loop();
    }
    return 0;
}
