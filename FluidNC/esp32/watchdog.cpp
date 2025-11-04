#include <esp_task_wdt.h>

void feed_watchdog() {
    esp_task_wdt_reset();
}
