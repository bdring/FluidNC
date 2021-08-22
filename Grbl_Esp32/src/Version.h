const char* const VERSION       = "2.1b";
const char* const VERSION_BUILD = "20210808";

#ifndef GIT_REV
static_assert(false,
              "FluidNC compilation is only supported on PlatformIO.\nSee "
              "https://github.com/bdring/Grbl_Esp32/wiki/Compiling-with-PlatformIO .");
#    define GIT_REV "Unsupported"
#    define GIT_TAG "0.0"
#endif
