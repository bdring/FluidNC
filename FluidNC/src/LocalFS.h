#pragma once

#ifdef USE_LITTLEFS
#    include <LittleFS.h>
#    define LocalFS LittleFS
#    define LOCALFS_NAME "LittleFS"
#    define LOCALFS_PREFIX "/littlefs/"
#else
#    include <SPIFFS.h>
#    define LocalFS SPIFFS
#    define LOCALFS_NAME "SPIFFS"
#    define LOCALFS_PREFIX "/spiffs/"
#endif
