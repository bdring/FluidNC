#pragma once

#include <Stream.h>

class Channel : public Stream {
public:
    static const int maxLine = 255;

private:
    char   _line[maxLine];
    size_t _linelen;
    int    _line_num;

public:
    Channel() : _linelen(0), _line_num(0) {}
    virtual ~Channel() = default;

    virtual Channel* pollLine(char* line);
};
