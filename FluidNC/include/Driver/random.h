#pragma once

#include <cstdint>

// 32 bits from the platform's entropy source: the hardware RNG on ESP32 and
// RP2040, std::random_device on hosted builds.  Arduino's random() is not a
// substitute: on RP2040 and in the posix emulator it is an unseeded rand(),
// which repeats the same sequence on every boot.
uint32_t random_u32();
