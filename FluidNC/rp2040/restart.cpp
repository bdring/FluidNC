#include "Driver/restart.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include <rp2040.h>
// Trigger a software reset
void restart() {
    watchdog_reboot(0, 0, 10);
    while (1) {}
}

bool restart_was_panic() {
    // return rp2040.getResetReason() == >>>;
    return false;
}
