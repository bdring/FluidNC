#include "esp_partition.h"
#include "esp_littlefs.h"
#include "wdt.h"
#include "src/Logging.h"

// Remember the partition label of the littlefs filesystem -
// typically littlefs or spiffs - so we can pass it to esp_littlefs_info
const char* littlefs_label;

bool littlefs_format(const char* partition_label) {
    esp_err_t err;
    disable_core0_WDT();
    if (partition_label) {
        log_debug("esp_littlefs_format " << partition_label);
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

bool littlefs_mount(const char* label, bool format) {
    esp_vfs_littlefs_conf_t conf = { .base_path = "/littlefs", .partition_label = label, .format_if_mount_failed = format };

    esp_err_t err = esp_vfs_littlefs_register(&conf);

    if (!err) {
        littlefs_label = label;
    }
    return err;
}
void littlefs_unmount() {
    esp_vfs_littlefs_unregister("littlefs");
}
