#pragma once

#include <cstdint>
#include <vector>
#include <mutex>

#include "esp32-hal-i2c.h"
#include "Stream.h"
#include "../FluidNC/test/TestFramework.h"

class TwoWire : public Stream {
    bool                 inTransmission = false;
    std::vector<uint8_t> receivedData;
    std::vector<uint8_t> sentData;
    std::mutex           mut;

    using ResponseHandler = void (*)(TwoWire* theWire, std::vector<uint8_t>& data, void* userData);
    void*           handlerUserData;
    ResponseHandler handler;

public:
    TwoWire(uint8_t bus_num);
    ~TwoWire();

    // For unit tests:
    void                 Send(std::vector<uint8_t> data);
    void                 Send(uint8_t value);
    size_t               SendSize() { return receivedData.size(); }
    std::vector<uint8_t> Receive();
    size_t               ReceiveSize() { return sentData.size(); }
    void                 Clear();
    void                 SetResponseHandler(ResponseHandler handler, void* userData) {
        this->handlerUserData = userData;
        this->handler         = handler;
    }

    // TwoWire interface:

    // call setPins() first, so that begin() can be called without arguments from libraries
    bool setPins(int sda, int scl);

    bool begin(int sda = -1, int scl = -1, uint32_t frequency = 0);  // returns true, if successful init of i2c bus
    bool begin(uint8_t slaveAddr, int sda = -1, int scl = -1, uint32_t frequency = 0);
    bool end();

    void     setTimeOut(uint16_t timeOutMillis);  // default timeout of i2c transactions is 50ms
    uint16_t getTimeOut();

    bool     setClock(uint32_t);
    uint32_t getClock();

    void beginTransmission(uint16_t address);
    void beginTransmission(uint8_t address);
    void beginTransmission(int address);

    uint8_t endTransmission(bool sendStop);
    uint8_t endTransmission(void);

    size_t  requestFrom(uint16_t address, size_t size, bool sendStop);
    uint8_t requestFrom(uint16_t address, uint8_t size, bool sendStop);
    uint8_t requestFrom(uint16_t address, uint8_t size, uint8_t sendStop);
    size_t  requestFrom(uint8_t address, size_t len, bool stopBit);
    uint8_t requestFrom(uint16_t address, uint8_t size);
    uint8_t requestFrom(uint8_t address, uint8_t size, uint8_t sendStop);
    uint8_t requestFrom(uint8_t address, uint8_t size);
    uint8_t requestFrom(int address, int size, int sendStop);
    uint8_t requestFrom(int address, int size);

    size_t write(uint8_t ch);
    size_t write(const uint8_t* buf, size_t size);
    int    available(void);
    int    read(void);
    int    peek(void);
    void   flush(void);

    inline size_t write(const char* s) { return write((uint8_t*)s, strlen(s)); }
    inline size_t write(unsigned long n) { return write((uint8_t)n); }
    inline size_t write(long n) { return write((uint8_t)n); }
    inline size_t write(unsigned int n) { return write((uint8_t)n); }
    inline size_t write(int n) { return write((uint8_t)n); }
};

extern TwoWire Wire;
extern TwoWire Wire1;
