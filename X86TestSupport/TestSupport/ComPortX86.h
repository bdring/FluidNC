#pragma once
#include "src/Channel.h"
#include <Windows.h>

class ComPortX86 : public Channel
{
public:
    ComPortX86(const char *pPort);
    ComPortX86();
    ~ComPortX86();
    virtual size_t write(uint8_t c) override;
    virtual int  read() override;
    virtual int  available() { return true; }
    virtual int  peek()      { return 0; }
    virtual void flush()     { return; }
    virtual int rx_buffer_available() { return 0; }
private:

    HANDLE hSerial;
};

