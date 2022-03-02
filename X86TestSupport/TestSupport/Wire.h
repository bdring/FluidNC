#pragma once

#include <cstdint>
#include "esp32-hal-i2c.h"
#include "Stream.h"

class TwoWire : public Stream {
public:
    TwoWire(uint8_t bus_num) {}
    ~TwoWire() {}

    //call setPins() first, so that begin() can be called without arguments from libraries
    bool setPins(int sda, int scl) {}

    bool begin(int sda = -1, int scl = -1, uint32_t frequency = 0) { return true; }  // returns true, if successful init of i2c bus
    bool begin(uint8_t slaveAddr, int sda = -1, int scl = -1, uint32_t frequency = 0) { return true; }
    bool end() { return true; }

    void     setTimeOut(uint16_t timeOutMillis) {}  // default timeout of i2c transactions is 50ms
    uint16_t getTimeOut() { return 0; }

    bool     setClock(uint32_t) {}
    uint32_t getClock() { return 0; }

    void beginTransmission(uint16_t address) {}
    void beginTransmission(uint8_t address) {}
    void beginTransmission(int address) {}

    uint8_t endTransmission(bool sendStop) { return 0; }
    uint8_t endTransmission(void) { return 0; }

    size_t  requestFrom(uint16_t address, size_t size, bool sendStop) { return 0; }
    uint8_t requestFrom(uint16_t address, uint8_t size, bool sendStop) { return 0; }
    uint8_t requestFrom(uint16_t address, uint8_t size, uint8_t sendStop) { return 0; }
    size_t  requestFrom(uint8_t address, size_t len, bool stopBit) { return 0; }
    uint8_t requestFrom(uint16_t address, uint8_t size) { return 0; }
    uint8_t requestFrom(uint8_t address, uint8_t size, uint8_t sendStop) { return 0; }
    uint8_t requestFrom(uint8_t address, uint8_t size) { return 0; }
    uint8_t requestFrom(int address, int size, int sendStop) { return 0; }
    uint8_t requestFrom(int address, int size) { return 0; }

    size_t write(uint8_t) { return 0; }
    size_t write(const uint8_t*, size_t) { return 0; }
    int    available(void) { return 0; }
    int    read(void) { return 0; }
    int    peek(void) { return 0; }
    void   flush(void) {}

    inline size_t write(const char* s) { return write((uint8_t*)s, strlen(s)); }
    inline size_t write(unsigned long n) { return write((uint8_t)n); }
    inline size_t write(long n) { return write((uint8_t)n); }
    inline size_t write(unsigned int n) { return write((uint8_t)n); }
    inline size_t write(int n) { return write((uint8_t)n); }
};

extern TwoWire Wire;
extern TwoWire Wire1;
