#include <atomic>
volatile bool g_should_exit = false;
#include "Driver/restart.h"
#include <cstdlib>
#include <cstdio>


#include "freertos/task.h"
void restart() {
    printf("Rastarting (exiting for now)\n");
    g_should_exit = true;
    extern void stepTimerShutdown();
    stepTimerShutdown();
    cleanup_threads();
    exit(0);
}

bool restart_was_panic() {
    return false;
}
