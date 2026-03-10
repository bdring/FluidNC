#include "Channel.h"  // Include the mock Channel definition first
#include "Logging.h"

// Dummy channel instance for logging stubs
Channel dummyChannel;

LogStream::LogStream(Channel& channel, MsgLevel level) 
    : _channel(channel), _line(nullptr) {}

LogStream::LogStream(Channel& channel, const char* name) 
    : _channel(channel), _line(nullptr) {}

LogStream::LogStream(Channel& channel, MsgLevel level, const char* name) 
    : _channel(channel), _line(nullptr) {}

LogStream::LogStream(MsgLevel level, const char* name) 
    : _channel(dummyChannel), _line(nullptr) {}

size_t LogStream::write(uint8_t c) {
    return 1;  // Pretend we wrote 1 byte
}

LogStream::~LogStream() {}
