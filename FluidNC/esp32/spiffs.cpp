#include "esp_spiffs.h"
#include "esp_log.h"
#include "wdt.h"
#include "src/Config.h"

bool spiffs_format(const char* partition_label) {
    disable_core0_WDT();
    esp_err_t err = esp_spiffs_format(partition_label);
    enable_core0_WDT();
    if (err) {
        log_info("SPIFFS format failed: " << esp_err_to_name(err));
        return true;
    }
    return false;
}
bool spiffs_mount(const char* label, bool format) {
    esp_log_level_set("SPIFFS", ESP_LOG_NONE);
    esp_vfs_spiffs_conf_t conf = { .base_path = "/spiffs", .partition_label = label, .max_files = 2, .format_if_mount_failed = format };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err) {
        return true;
    }
    return false;
}
void spiffs_unmount() {
    esp_vfs_spiffs_unregister("spiffs");
}
