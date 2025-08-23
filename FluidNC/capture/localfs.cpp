#include "Driver/localfs.h"
#include <cstdint>

const char* localfsName = "";

bool localfs_format(const char* fsname) {
    return false;
}
bool localfs_mount() {
    return false;
}
void localfs_unmount() {};

std::uintmax_t localfs_size() {
    return 200000;
}
const char* canonicalPath(const char* filename, const char* defaultFs) {
    return filename;
}

bool sd_init_slot(uint32_t freq_hz, int cs_pin, int cd_pin = -1, int wp_pin = -1) {
    return true;
}
void sd_deinit_slot() {}
void sd_unmount() {}

std::error_code sd_mount(int max_files) {
    return {};
};

#include "Driver/delay_usecs.h"
static int counter = 0;
uint32_t   ticks_per_us;

void timing_init() {
    ticks_per_us = 1;
}

void delay_us(int32_t us) {
    spinUntil(usToEndTicks(us));
}

int32_t usToCpuTicks(int32_t us) {
    return us * ticks_per_us;
}

int32_t usToEndTicks(int32_t us) {
    return getCpuTicks() + usToCpuTicks(us);
}

void spinUntil(int32_t endTicks) {
    while ((getCpuTicks() - endTicks) < 0) {}
}

int32_t getCpuTicks() {
    return ++counter;
}

#include "Driver/i2s_out.h"
int i2s_out_init(i2s_out_init_t* params) {
    return 0;
}
