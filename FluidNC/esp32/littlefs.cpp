#include "esp_partition.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "wdt.h"
#include "Driver/localfs.h"
#include "Config.h"

// Remember the partition label of the littlefs filesystem -
// typically littlefs or spiffs - so we can pass it to esp_littlefs_info
const char* littlefs_label;

bool littlefs_format(const char* partition_label) {
    esp_log_level_set("esp_littlefs", ESP_LOG_NONE);
    esp_err_t err;
    disable_core0_WDT();
    err = esp_littlefs_format(partition_label);
    enable_core0_WDT();
    if (err) {
        log_debug("LittleFS format in " << partition_label << " partition failed: " << esp_err_to_name(err));
        return true;
    }
    return false;
}

bool littlefs_mount(const char* label, bool format) {
    esp_log_level_set("esp_littlefs", ESP_LOG_NONE);
    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path               = "/littlefs";
    conf.partition_label         = label;
    conf.format_if_mount_failed  = format;

    esp_err_t err = esp_vfs_littlefs_register(&conf);

    if (!err) {
        littlefs_label = label;
    }
    return err;
}
void littlefs_unmount() {
    esp_vfs_littlefs_unregister(littlefs_label);
}
