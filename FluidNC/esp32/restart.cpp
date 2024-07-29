#include "Driver/restart.h"
#include "esp_system.h"

void restart() {
    esp_restart();
    while (1) {}
}

bool restart_was_panic() {
    return esp_reset_reason() == ESP_RST_PANIC;
}
