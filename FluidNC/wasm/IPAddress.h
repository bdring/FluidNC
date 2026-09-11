#pragma once

// capture/arduino/Arduino.h unconditionally includes <IPAddress.h>, but the
// shared copy (capture/IPAddress.hx) is currently disabled repo-wide, and
// the wasm port's core-only scope has no real networking (no AsyncTCP/WiFi)
// to back a real one anyway. This is a local copy of that same disabled
// stub, kept in FluidNC/wasm/ (ahead of capture/arduino on the include
// path) purely so Arduino.h resolves -- not a statement that it should be
// re-enabled repo-wide.

#include <cstdint>
#include <cstring>
#include "WString.h"

class IPAddress {
private:
    union {
        uint8_t  bytes[4];  // IPv4 address
        uint32_t dword;
    } _address;

    uint8_t* raw_address() { return _address.bytes; }

public:
    IPAddress() { _address.dword = 0; }
    IPAddress(uint8_t first_octet, uint8_t second_octet, uint8_t third_octet, uint8_t fourth_octet) {
        _address.bytes[0] = first_octet;
        _address.bytes[1] = second_octet;
        _address.bytes[2] = third_octet;
        _address.bytes[3] = fourth_octet;
    }
    IPAddress(uint32_t address) { _address.dword = address; }
    IPAddress(const uint8_t* address) { memcpy(_address.bytes, address, 4); }
    virtual ~IPAddress() {}

    bool fromString(const char* address) { throw "not implemented"; }
    bool fromString(const String& address) { return fromString(address.c_str()); }

    operator uint32_t() const { return _address.dword; }
    bool operator==(const IPAddress& addr) const { return _address.dword == addr._address.dword; }
    bool operator==(const uint8_t* addr) const { return (*this) == IPAddress(addr); }

    uint8_t  operator[](int index) const { return _address.bytes[index]; }
    uint8_t& operator[](int index) { return _address.bytes[index]; }

    IPAddress& operator=(const uint8_t* address) {
        memcpy(_address.bytes, address, 4);
        return *this;
    }
    IPAddress& operator=(uint32_t address) {
        _address.dword = address;
        return *this;
    }

    String toString() const { throw "not implemented"; }
};

const IPAddress INADDR_NONE(0, 0, 0, 0);
