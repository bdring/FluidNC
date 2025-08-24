#include "fluidnc_vfs_ops.h"
#include <string.h>
#include "Config.h"

#include "esp_spiffs.h"
#include "ff.h"
#include "esp_littlefs.h"
#include "Driver/littlefs.h"

bool isSPIFFS(const char* mountpoint) {
    return !strcmp(mountpoint, "spiffs");
}
bool isSD(const char* mountpoint) {
    return !strcmp(mountpoint, "sd");
}
bool isLittleFS(const char* mountpoint) {
    return !strcmp(mountpoint, "littlefs");
}
// cppcheck-suppress unusedFunction
bool fluidnc_vfs_stats(const char* mountpoint, uint64_t& total, uint64_t& used) {
    if (isSD(mountpoint)) {
        FATFS* fsinfo;
        DWORD  fre_clust;
        if (f_getfree("0:", &fre_clust, &fsinfo) != 0) {
            return false;
        }
        uint64_t clsize   = fsinfo->csize * fsinfo->ssize;
        uint32_t total_cl = fsinfo->n_fatent - 2;

        total = clsize * total_cl;
        used  = clsize * (total_cl - fsinfo->free_clst);
        return true;
    }
    size_t stotal, sused;

    if (isSPIFFS(mountpoint)) {
        if (esp_spiffs_info("spiffs", &stotal, &sused) != ESP_OK) {
            return false;
        }
    } else if (isLittleFS(mountpoint)) {
        if (esp_littlefs_info(littlefs_label, &stotal, &sused)) {
            return false;
        }
    } else {
        return false;
    }
    total = stotal;
    used  = sused;
    return true;
}
