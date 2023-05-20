#pragma once

// This contains definitions of "very platform specific defines", that cannot be dealth with some other way.

#ifdef ESP32

#    define WEAK_LINK __attribute__((weak))
#    include <esp_attr.h>  // IRAM_ATTR

#else

#    define WEAK_LINK 
#    define IRAM_ATTR

#endif
