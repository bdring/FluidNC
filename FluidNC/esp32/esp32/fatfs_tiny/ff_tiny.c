// Copyright (c) 2026 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Recompile the ESP-IDF v4.4.7 copy of Chan's FatFs engine with FF_FS_TINY == 1.
//
// The Arduino-ESP32 core 2.0.17 precompiled libfatfs.a is built with
// CONFIG_FATFS_PER_FILE_CACHE == 1, so ff.h sets FF_FS_TINY == 0 and every open
// FIL carries a private BYTE buf[FF_MAX_SS] window.  On this SDK FF_MAX_SS is
// MAX(FF_SS_SDCARD, FF_SS_WL) == MAX(512, CONFIG_WL_SECTOR_SIZE) == 4096, so that
// is ~4 KB of internal DRAM per concurrently open SD file.
//
// Overriding CONFIG_FATFS_PER_FILE_CACHE to 0 here makes ff.h evaluate
// FF_FS_TINY as 1: the per-FIL buffer disappears and all file data transfers go
// through the single shared FATFS.win[FF_MAX_SS] window (allocated once per
// mount regardless).  sdkconfig.h is force-included on the command line before
// this file body, so the #undef/#define below wins.
//
// This translation unit provides the same public symbols (f_open, f_read, ...)
// as the ff.c.obj member of the precompiled libfatfs.a.  Because it is a direct
// link input it is bound before the archive is searched, so the stale archive
// member is never pulled in (same technique as FluidNC/stdfs17).
//
// vfs_fat_tiny.c MUST apply the identical override so that both translation
// units agree on sizeof(FIL) / the FIL layout.
//
// ff.c is the verbatim components/fatfs/src/ff.c from the esp-idf v4.4.7 tag
// (FatFs R0.13c).  Its ff.h / ffconf.h are byte-identical to the headers shipped
// in framework-arduinoespressif32@3.20017.241212, so the recompiled object is
// ABI-identical to the archive member apart from this one setting.

// Pull in sdkconfig.h first (it is #pragma once) and flip the option before
// anything else includes it.  ffconf.h does #include "sdkconfig.h" at its top,
// but that re-include is then a no-op, so this definition is the one ffconf.h
// sees when it computes FF_FS_TINY == (!CONFIG_FATFS_PER_FILE_CACHE).
#include "sdkconfig.h"
#undef CONFIG_FATFS_PER_FILE_CACHE
#define CONFIG_FATFS_PER_FILE_CACHE 0

#include "ff.c"

// Guard: prove the override took effect.  With FF_FS_TINY == 0 and this SDK's
// FF_MAX_SS == 4096, sizeof(FIL) is > 4 KB; with FF_FS_TINY == 1 it is well
// under 128 bytes.
_Static_assert(FF_FS_TINY == 1, "FF_FS_TINY override did not take effect");
_Static_assert(sizeof(FIL) < 256, "FIL still carries a per-file sector buffer");
