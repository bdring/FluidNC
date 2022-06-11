#pragma once

#include <cstdint>
#include "WString.h"

class IPAddress {
private:
    union {
        uint8_t  bytes[4];  // IPv4 address
        uint32_t dword;
    } _address;

    // Access the raw byte array containing the address.  Because this returns a pointer
    // to the internal structure rather than a copy of the address this function should only
    // be used when you know that the usage of the returned uint8_t* will be transient and not
    // stored.
    uint8_t* raw_address() { return _address.bytes; }

public:
    // Constructors
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

    // Overloaded cast operator to allow IPAddress objects to be used where a pointer
    // to a four-byte uint8_t array is expected
         operator uint32_t() const { return _address.dword; }
    bool operator==(const IPAddress& addr) const { return _address.dword == addr._address.dword; }
    bool operator==(const uint8_t* addr) const { return (*this) == IPAddress(addr); }

    // Overloaded index operator to allow getting and setting individual octets of the address
    uint8_t  operator[](int index) const { return _address.bytes[index]; }
    uint8_t& operator[](int index) { return _address.bytes[index]; }

    // Overloaded copy operators to allow initialisation of IPAddress objects from other types
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
