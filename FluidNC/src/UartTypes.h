#pragma once

#include <string>

enum class UartData : int {
    Bits5 = 5,
    Bits6 = 6,
    Bits7 = 7,
    Bits8 = 8,
};

enum class UartStop : int {
    Bits1   = 1,
    Bits1_5 = 3,
    Bits2   = 2,
};

enum class UartParity : int {
    None = 0,
    Even = 2,
    Odd  = 1,
};

const char* decodeUartMode(std::string_view str, UartData& wordLength, UartParity& parity, UartStop& stopBits);
std::string encodeUartMode(UartData wordLength, UartParity parity, UartStop stopBits);
