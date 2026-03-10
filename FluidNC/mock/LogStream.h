#pragma once

#include <cstdint>

// Forward declarations
class Channel;
enum class MsgLevel : int {
    MsgLevelNone    = 0,
    MsgLevelError   = 1,
    MsgLevelWarning = 2,
    MsgLevelInfo    = 3,
    MsgLevelDebug   = 4,
    MsgLevelVerbose = 5,
};

// Mock LogStream implementation - provides constructors and destructor
class LogStream {
public:
    LogStream(Channel& channel, MsgLevel level);
    LogStream(Channel& channel, const char* name);
    LogStream(Channel& channel, MsgLevel level, const char* name);
    LogStream(MsgLevel level, const char* name);
    size_t write(uint8_t c);
    ~LogStream();

    // Support stream operators
    template <typename T>
    LogStream& operator<<(const T& value) {
        return *this;  // No-op
    }
};
