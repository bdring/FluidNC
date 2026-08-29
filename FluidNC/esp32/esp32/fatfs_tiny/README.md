# fatfs_tiny — recompiled FatFs with FF_FS_TINY == 1 (esp32 / Arduino core 2.0.17 only)

## Why

On `framework-arduinoespressif32@3.20017.241212` (ESP-IDF v4.4.7) the precompiled
`tools/sdk/esp32/lib/libfatfs.a` is built with:

    CONFIG_FATFS_PER_FILE_CACHE = 1   ->  FF_FS_TINY = 0
    CONFIG_WL_SECTOR_SIZE       = 4096
    FF_MAX_SS = MAX(FF_SS_SDCARD, FF_SS_WL) = MAX(512, 4096) = 4096

With `FF_FS_TINY == 0`, `struct FIL` embeds `BYTE buf[FF_MAX_SS]`, i.e. **~4 KB of
internal DRAM per concurrently open SD file**. `esp_vfs_fat_register()` allocates
`max_files * sizeof(FIL)` up front at mount time; `f_rename()` allocates two more
transient `FIL`s. This is why `sd_mount()`'s default `max_files` had to drop from
3 to 2 (commit 62bf3f6a).

Setting `FF_FS_TINY == 1` removes the per-`FIL` buffer; all file data transfers
then share the single `FATFS.win[FF_MAX_SS]` window that is allocated once per
mount either way. Trade-off: more re-reads of that shared window when file I/O is
interleaved across files or mixed with directory traversal -> more SD bus
traffic. Needs a reliability soak on real hardware, not just a clean build.

`CONFIG_FATFS_PER_FILE_CACHE` is a real Kconfig option, but it cannot be changed
for the classic `esp32` env because that build links the frozen archive rather
than compiling the component.

## How

- `ff.c`, `vfs_fat.c` — **verbatim** from the `v4.4.7` tag of `espressif/esp-idf`
  (`components/fatfs/src/ff.c` = FatFs R0.13c, `components/fatfs/vfs/vfs_fat.c`).
  Do not edit. Their `ff.h` / `ffconf.h` were verified byte-identical to the
  headers in `framework-arduinoespressif32@3.20017.241212`, so the recompiled
  objects are ABI-identical to the archive members apart from `FF_FS_TINY`.
- `ff_tiny.c`, `vfs_fat_tiny.c` — thin wrappers: `#undef` / `#define
  CONFIG_FATFS_PER_FILE_CACHE 0`, then `#include` the verbatim file. Only these
  two wrappers are compiled (see `[common_esp32] build_src_filter` in
  `platformio.ini`); the verbatim `.c` files are not built on their own.
- Link-order override: the wrapper objects are direct link inputs, so `f_*` /
  `esp_vfs_fat_*` resolve from them and the `ff.c.obj` / `vfs_fat.c.obj` members
  of `libfatfs.a` are never pulled. Same technique as `FluidNC/stdfs17`. The
  remaining `libfatfs.a` members (`diskio*`, `ffsystem`, `ffunicode`,
  `vfs_fat_sdmmc`, `vfs_fat_spiflash`) are unchanged and `FIL` never crosses
  their interfaces.

## Scope

esp32 (`[common_esp32]`) only. The `_s3` envs use pioarduino / ESP-IDF v5.5 with
FatFs R0.15 and a different `FIL`; this R0.13c source must not be wired into
those builds.

## Updating

If the classic `esp32` env's Arduino core / IDF version changes, re-fetch both
files from the matching `espressif/esp-idf` tag and re-verify `ff.h` / `ffconf.h`
against `tools/sdk/esp32/include/fatfs/src/`.
