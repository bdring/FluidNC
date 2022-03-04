#include "Wire.h"

#include <mutex>

TwoWire Wire(0);
TwoWire Wire1(1);

TwoWire::TwoWire(uint8_t bus_num) : handler(nullptr) {}

// For unit tests:
void TwoWire::Send(std::vector<uint8_t> data) {
    for (auto it : data) {
        Send(it);
    }
}
void TwoWire::Send(uint8_t value) {
    std::lock_guard<std::mutex> guard(mut);
    receivedData.push_back(value);
}
std::vector<uint8_t> TwoWire::Receive() {
    std::lock_guard<std::mutex> guard(mut);
    std::vector<uint8_t>        data;
    std::swap(sentData, data);
    return data;
}

void TwoWire::Clear() {
    std::lock_guard<std::mutex> guard(mut);
    sentData.clear();
    receivedData.clear();
    handler         = nullptr;
    handlerUserData = nullptr;
}

// TwoWire interface:

// call setPins() first, so that begin() can be called without arguments from libraries
bool TwoWire::setPins(int sda, int scl) {
    return true;
}

bool TwoWire::begin(int sda, int scl, uint32_t frequency) {
    return true;
}  // returns true, if successful init of i2c bus

bool TwoWire::begin(uint8_t slaveAddr, int sda, int scl, uint32_t frequency) {
    return true;
}
bool TwoWire::end() {
    return true;
}

void     TwoWire::setTimeOut(uint16_t timeOutMillis) {}  // default timeout of i2c transactions is 50ms
uint16_t TwoWire::getTimeOut() {
    return 0;
}

bool TwoWire::setClock(uint32_t) {
    return true;
}
uint32_t TwoWire::getClock() {
    return 0;
}

void TwoWire::beginTransmission(uint16_t address) {
    Assert(!inTransmission, "Already in a transmission");
    inTransmission = true;
}
void TwoWire::beginTransmission(uint8_t address) {
    Assert(!inTransmission, "Already in a transmission");
    inTransmission = true;
}
void TwoWire::beginTransmission(int address) {
    Assert(!inTransmission, "Already in a transmission");
    inTransmission = true;
}

uint8_t TwoWire::endTransmission(bool sendStop) {
    Assert(inTransmission, "Should be in a transmission");
    inTransmission = false;
    return 0;
}
uint8_t TwoWire::endTransmission(void) {
    Assert(inTransmission, "Should be in a transmission");
    inTransmission = false;
    return 0;
}

size_t TwoWire::requestFrom(uint16_t address, size_t size, bool sendStop) {
    std::lock_guard<std::mutex> guard(mut);

    auto available = receivedData.size();
    if (available > size) {
        available = size;
    }
    return available;
}
uint8_t TwoWire::requestFrom(uint16_t address, uint8_t size, bool sendStop) {
    return uint8_t(requestFrom(address, size_t(size), sendStop));
}
uint8_t TwoWire::requestFrom(uint16_t address, uint8_t size, uint8_t sendStop) {
    return uint8_t(requestFrom(address, size_t(size), sendStop != 0));
}
size_t TwoWire::requestFrom(uint8_t address, size_t len, bool stopBit) {
    return uint8_t(requestFrom(uint16_t(address), size_t(len), stopBit));
}
uint8_t TwoWire::requestFrom(uint16_t address, uint8_t size) {
    return uint8_t(requestFrom(address, size_t(size), false));
}
uint8_t TwoWire::requestFrom(uint8_t address, uint8_t size, uint8_t sendStop) {
    return uint8_t(requestFrom(address, size_t(size), sendStop != 0));
}
uint8_t TwoWire::requestFrom(uint8_t address, uint8_t size) {
    return uint8_t(requestFrom(address, size_t(size), false));
}
uint8_t TwoWire::requestFrom(int address, int size, int sendStop) {
    return uint8_t(requestFrom(uint16_t(address), size_t(size), sendStop != 0));
}
uint8_t TwoWire::requestFrom(int address, int size) {
    return uint8_t(requestFrom(uint16_t(address), size_t(size), false));
}

size_t TwoWire::write(uint8_t ch) {
    {
        Assert(inTransmission, "Should be in a transmission");
        std::lock_guard<std::mutex> guard(mut);
        sentData.push_back(ch);
    }

    if (handler) {
        (*handler)(this, sentData, handlerUserData);
    }
    return 0;
}

size_t TwoWire::write(const uint8_t* buf, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        write(buf[i]);
    }
    return size;
}
int TwoWire::available(void) {
    std::lock_guard<std::mutex> guard(mut);
    return receivedData.size();
}
int TwoWire::read(void) {
    std::lock_guard<std::mutex> guard(mut);
    if (receivedData.size()) {
        auto result = receivedData[0];
        receivedData.erase(receivedData.begin());
        return result;
    } else {
        return -1;
    }
}
int TwoWire::peek(void) {
    std::lock_guard<std::mutex> guard(mut);
    if (receivedData.size()) {
        auto result = receivedData[0];
        return result;
    } else {
        return -1;
    }
}
void TwoWire::flush(void) {}

TwoWire::~TwoWire() {}

extern TwoWire Wire;
extern TwoWire Wire1;
