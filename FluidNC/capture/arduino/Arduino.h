#pragma once

// Real Arduino cores (ESP32, rp2040-arduino-pico, Arduino-Emulator) pull in
// their own Common.h-equivalent from their umbrella Arduino.h, which is
// what gives extern "C" linkage to a .cpp's `void setup() {}`/`void loop()
// {}` definitions matching the extern "C" declarations every core exposes
// for them -- this capture/arduino one was the odd one out, missing that,
// so any definition of setup()/loop()/millis()/etc. here silently got
// mangled C++ linkage instead, an undefined-symbol link error waiting to
// happen the first time anything using it (capture/freertos/task.cpp,
// Main.cpp) got compiled all the way to link, which apparently never
// happened before (windows_x86, the only other consumer, has no working
// toolchain registered to build with in this environment).
#include "../Common.h"

#include <IPAddress.h>
#include <Print.h>
#include <Stream.h>
#include <WString.h>

unsigned long millis();
