#pragma once

#include <cstdint>
#include <cstdlib>

uint16_t crc16_ccitt(const uint8_t* buf, size_t len);