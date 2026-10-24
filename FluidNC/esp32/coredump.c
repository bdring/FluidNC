// Copyright 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.
//
// Wrappers for the ESP-IDF coredump routines, via -Wl,--wrap=esp_core_dump_init
// and -Wl,--wrap=esp_core_dump_write in platformio.ini.
//
// The panic handler calls esp_core_dump_write().  We always capture the
// backtrace into RTC_NOINIT memory that survives the reset, for $BS and the
// startup log.  If the partition table has a coredump partition, we then
// let IDF write a full ELF coredump there.  Without one, we skip IDF's
// coredump entirely, which also suppresses its "no partition" complaints.

#include <stddef.h>
#include <string.h>
#include "esp_err.h"
#include "esp_attr.h"
#include "esp_partition.h"
#include "esp_private/panic_internal.h"
#include "esp_debug_helpers.h"
#include "Driver/backtrace.h"

// On Xtensa targets we can access the exception frame
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
#include "xtensa/xtensa_context.h"
#define BACKTRACE_XTENSA 1
#endif

// Magic value + CRC to validate the saved backtrace across resets
#define BACKTRACE_MAGIC 0x42545243  // "BTRC"

typedef struct {
    uint32_t    magic;
    uint32_t    crc;
    backtrace_t bt;
} backtrace_record_t;

static RTC_NOINIT_ATTR backtrace_record_t _saved_bt;

static bool _have_coredump_partition = false;

// Simple CRC-32 over the backtrace data.  IRAM because it runs during panic.
static uint32_t IRAM_ATTR backtrace_crc(const backtrace_t* bt) {
    const uint8_t* p   = (const uint8_t*)bt;
    size_t         len = sizeof(backtrace_t);
    uint32_t       crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return crc ^ 0xFFFFFFFF;
}

static bool backtrace_valid(void) {
    if (_saved_bt.magic != BACKTRACE_MAGIC) {
        return false;
    }
    return _saved_bt.crc == backtrace_crc(&_saved_bt.bt);
}

bool backtrace_available(void) {
    return backtrace_valid();
}

bool backtrace_get(backtrace_t* bt) {
    if (!backtrace_valid()) {
        return false;
    }
    *bt = _saved_bt.bt;
    return true;
}

void backtrace_clear(void) {
    _saved_bt.magic = 0;
}

static void IRAM_ATTR backtrace_save(panic_info_t* info) {
    memset(&_saved_bt, 0, sizeof(_saved_bt));

    if (!info || !info->frame) {
        return;
    }

    backtrace_t* bt = &_saved_bt.bt;

#ifdef BACKTRACE_XTENSA
    XtExcFrame* frame = (XtExcFrame*)info->frame;

    bt->pc       = frame->pc;
    bt->excvaddr = frame->excvaddr;
    bt->exccause = frame->exccause;

    // Initialize backtrace walking from the exception frame
    esp_backtrace_frame_t bt_frame;
    bt_frame.pc        = frame->pc;
    bt_frame.sp        = frame->a1;
    bt_frame.next_pc   = frame->a0;
    bt_frame.exc_frame = info->frame;

    // First entry is the faulting PC
    bt->addresses[0]  = bt_frame.pc;
    bt->num_addresses = 1;

    // Walk the call stack
    while (bt->num_addresses < BACKTRACE_MAX_ADDRESSES) {
        if (!esp_backtrace_get_next_frame(&bt_frame)) {
            break;
        }
        bt->addresses[bt->num_addresses++] = bt_frame.pc;
        if (bt_frame.next_pc == 0) {
            break;
        }
    }
#else
    // Non-Xtensa: just save what the panic_info gives us
    bt->pc            = (uint32_t)(uintptr_t)info->addr;
    bt->excvaddr      = 0;
    bt->exccause      = (uint32_t)info->exception;
    bt->addresses[0]  = bt->pc;
    bt->num_addresses = 1;
#endif

    // Seal with magic and CRC
    _saved_bt.crc   = backtrace_crc(bt);
    _saved_bt.magic = BACKTRACE_MAGIC;
}

void __real_esp_core_dump_init(void);
void __real_esp_core_dump_write(panic_info_t* info);

// Called at startup from IDF's system init.  The partition lookup is
// done here, not at panic time, where it might not be safe.
// cppcheck-suppress unusedFunction
void __wrap_esp_core_dump_init(void) {
    _have_coredump_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL) != NULL;
    if (_have_coredump_partition) {
        __real_esp_core_dump_init();
    }
}

// Called by the panic handler
// cppcheck-suppress unusedFunction
void IRAM_ATTR __wrap_esp_core_dump_write(panic_info_t* info) {
    backtrace_save(info);
    if (_have_coredump_partition) {
        __real_esp_core_dump_write(info);
    }
}
