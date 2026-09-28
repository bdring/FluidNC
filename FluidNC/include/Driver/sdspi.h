#pragma once

#include "Driver/fluidnc_gpio.h"
#include <system_error>

bool sd_init_slot(uint32_t freq_hz, pinnum_t cs_pin, pinnum_t cd_pin = INVALID_PINNUM, pinnum_t wp_pin = INVALID_PINNUM);
void sd_unmount();
void sd_deinit_slot();

// esp_vfs_fat_register() allocates max_files * sizeof(FIL) of internal DRAM up
// front at every mount, so the affordable default depends on what a FIL costs.
//
// With the precompiled libfatfs.a a FIL embeds a full sector buffer - ~4 KB
// each - which is why this had to drop from 3 to 2 in 62bf3f6a.  The classic
// esp32 env now links the FF_FS_TINY==1 rebuild instead (see
// FluidNC/esp32/esp32/fatfs_tiny/README.md), where a FIL carries no buffer at
// all, so that reduction no longer buys anything there.
//
// Give that env 4 back.  Two open files is tight: f_rename() allocates two
// transient FILs of its own, so renaming while a job holds a descriptor can
// fail for want of a slot rather than for any reason to do with the card.
//
// The tiny rebuild deliberately does not cover the _s3 envs, which use a
// different FatFs with a different FIL, so they keep 2.
#ifdef FLUIDNC_FATFS_TINY
std::error_code sd_mount(uint32_t max_files = 4);
#else
std::error_code sd_mount(uint32_t max_files = 2);
#endif
