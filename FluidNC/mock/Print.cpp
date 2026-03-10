// Out-of-line definitions for Print methods
#include "Print.h"
#include <cstring>

Print::~Print() {}

size_t Print::write(const uint8_t* buffer, size_t size) {
    return size;  // Mock: just return the size
}

size_t Print::print(const char* str) {
    return str ? strlen(str) : 0;  // Mock: return string length
}

size_t Print::print(double value, int digits) {
    return 0;  // Mock: no-op
}
