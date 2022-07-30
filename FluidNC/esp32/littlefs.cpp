#include "esp_partition.h"
#include "esp_littlefs.h"
#include "wdt.h"
#include "src/Logging.h"

bool littlefs_format(const char* partition_label) {
    esp_err_t err;
    disable_core0_WDT();
    if (partition_label) {
        err = esp_littlefs_format(partition_label);
    } else {
        err = esp_littlefs_format("littlefs");
        if (err) {
            auto part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);

            const char* label = part->label;
            log_info("Trying partition named " << label);
            err = esp_littlefs_format(label);
        }
    }
    enable_core0_WDT();
    if (err) {
        log_info("LittleFS format failed - " << esp_err_to_name(err));
        return true;
    }
    return false;
}

bool littlefs_mount() {
    esp_vfs_littlefs_conf_t conf = { .base_path = "/littlefs", .partition_label = "littlefs", .format_if_mount_failed = false };

    return esp_vfs_littlefs_register(&conf);
}
void littlefs_unmount() {
    esp_vfs_littlefs_unregister("littlefs");
}
