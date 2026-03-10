#pragma once

#include <cstdint>
#include <cstddef>

// Minimal Channel stub for testing - Logging.h needs this to exist
class Channel {
public:
    virtual ~Channel() = default;
    virtual size_t write(uint8_t c) { return 1; }
    virtual void sendLine(int level, const char* message) {}
};
