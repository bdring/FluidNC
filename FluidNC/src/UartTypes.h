#pragma once

#include <string>

enum class UartData : int32_t {
    Bits5 = 5,
    Bits6 = 6,
    Bits7 = 7,
    Bits8 = 8,
};

enum class UartStop : int32_t {
    Bits1   = 1,
    Bits1_5 = 3,
    Bits2   = 2,
};

enum class UartParity : int32_t {
    None = 0,
    Even = 2,
    Odd  = 1,
};

const char* decodeUartMode(std::string_view str, UartData& wordLength, UartParity& parity, UartStop& stopBits);
std::string encodeUartMode(UartData wordLength, UartParity parity, UartStop stopBits);
