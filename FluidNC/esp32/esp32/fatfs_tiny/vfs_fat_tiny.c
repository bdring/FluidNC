// Copyright (c) 2026 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Recompile the ESP-IDF v4.4.7 FatFs VFS glue with FF_FS_TINY == 1, matching
// ff_tiny.c.  vfs_fat.c is where the per-open-file storage is actually
// allocated:
//
//     esp_vfs_fat_register(): ff_memalloc(sizeof(vfs_fat_ctx_t) + max_files * sizeof(FIL))
//     vfs_fat_link()/rename(): ff_memalloc(sizeof(FIL)) x2
//
// so it must see the same reduced sizeof(FIL) as ff_tiny.c or the ctx array and
// the engine will disagree on the FIL layout.  See ff_tiny.c for the full
// rationale and provenance notes.
//
// Provides the same public symbols (esp_vfs_fat_register,
// esp_vfs_fat_unregister_path, ...) as the vfs_fat.c.obj member of the
// precompiled libfatfs.a; as a direct link input it is bound ahead of the
// archive member.

// See ff_tiny.c for why sdkconfig.h is pulled in first.
#include "sdkconfig.h"
#undef CONFIG_FATFS_PER_FILE_CACHE
#define CONFIG_FATFS_PER_FILE_CACHE 0

#include "vfs_fat.c"

// Must agree with ff_tiny.c on the FIL layout, or the ctx files[] array and the
// FatFs engine disagree about element size.
_Static_assert(sizeof(FIL) < 256, "FIL still carries a per-file sector buffer");
