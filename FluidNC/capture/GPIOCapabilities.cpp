#include "Platform.h"

#if MAX_N_GPIO == 49
#    include "../esp32/esp32s3/GPIOCapabilities.cpp"
#else
#    include "../esp32/esp32/GPIOCapabilities.cpp"
#endif
