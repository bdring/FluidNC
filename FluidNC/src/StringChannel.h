#pragma once

#include "Channel.h"
#include "Serial.h"
#include <string_view>
#include <cstdio>

/*
 * StringChannel: A Channel implementation that feeds G-code commands from a string.
 * Used to inject test commands into the POSIX build for automated testing.
 * 
 * The string is consumed character-by-character via read().
 * If the string doesn't end with a newline, one is automatically appended.
 */
class StringChannel : public Channel {
public:
    /*
     * Constructor: Takes the input string to be consumed.
     * If string is empty or all whitespace, the channel will immediately return EOF.
     */
    StringChannel(const std::string_view input) : Channel("StringChannel", false) {
        push(input);
        if (!input.empty() && input.back() != '\n') {
            push((uint8_t)'\n');
        }
    }

    // This is a no-op so the initial call to it does not clear the queue
    void flushRx() override {}

    void init() override { allChannels.registration(this); }

    Error pollLine(char* line) override {
        if (line) {
            auto ret = Channel::pollLine(line);
            return ret;
        }
        return Error::NoData;
    }

    // Print methods (Stream inherits from Print)
    size_t write(uint8_t c) override {
        // Send character to stderr
        return fputc(c, stderr) != EOF ? 1 : 0;
    }

    int read() override { return -1; }

    // Channel methods
    int rx_buffer_available() override { return 0; }
};
