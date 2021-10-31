#pragma once

#include <Stream.h>
#include "Error.h"  // Error

class Channel : public Stream {
public:
    static const int maxLine = 255;

private:
    char   _line[maxLine];
    size_t _linelen;

public:
    Channel() : _linelen(0) {}
    virtual ~Channel() = default;

    virtual Channel* pollLine(char* line);
    virtual void     ack(Error status);
};
