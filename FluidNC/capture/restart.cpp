#include "Driver/restart.h"
#include <cstdlib>
#include <cstdio>

void restart() {
    printf("Rastarting (exiting for now)\n");
    exit(0);
}

bool restart_was_panic() {
    return false;
}
