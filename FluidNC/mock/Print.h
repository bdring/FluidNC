#pragma once

#include <cstddef>
#include <cstdint>

// Minimal Print interface for mocking
class Print {
public:
    virtual ~Print();  // Declaration only - definition in Print.cpp
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t* buffer, size_t size);
    virtual size_t print(const char* str);
    virtual size_t print(double value, int digits);
};
