#include "../../build/config/sdkconfig.h"

// Arduino TinyUSB Hack to disable the Arduino USB support.
//
// Here's what's going on. Arduino decided it's a good idea to use a very old version of TinyUSB, and just
// check the SDKConfig to see if it's available. No component dependencies, no config options, no nothing;
// so that implies it's just a mess.
//
// In the meantime, we have Espressif updating TinyUSB as the managed component "tinyusb" or "esp_tinyusb".
// Guess what, the new version is not backwards compatible with the old version. So, if you try to use the
// new version, it will just not compile.
//
// Because Arduino doesn't use the proper dependency chain, there's no good way to fix this apart from
// hacking into the sdkconfig. That's exactly what this file is for. CMakeLists adds this folder as high
// priority, so ONLY arduino sees this sdkconfig.h file. The result is that Arduino doesn't implement any
// USB CDC/MSC devices, which is perfectly fine with me.
//
// In other words, this way we can still compile TinyUSB properly and use it as-is without having to fight
// with Arduino.

#undef CONFIG_TINYUSB_MSC_ENABLED
#undef CONFIG_TINYUSB_CDC_ENABLED
