// Copyright 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.
//
// Noop replacements for ESP-IDF coredump routines.
// This suppresses complaints about not being able to find a coredump partition.
// We don't want to waste space for such a partition, and the Arduino Framework
// enables coredumps.  We override that by stubbing out these routines.

#include <stddef.h>
#include "esp_err.h"
#include "esp_private/panic_internal.h"
#include "esp_core_dump_summary_port.h"

#ifdef __cplusplus
extern "C" {
#endif

void esp_core_dump_init(void) {}

void esp_core_dump_flash_init(void) {}
void esp_core_dump_to_flash(void* info) {}

esp_err_t esp_core_dump_image_check(void) {
    return ESP_ERR_NOT_FOUND;
}
esp_err_t esp_core_dump_image_get(size_t* out_addr, size_t* out_size) {
    return ESP_ERR_NOT_FOUND;
}
esp_err_t esp_core_dump_image_erase(void) {
    return ESP_OK;
}

#ifdef __cplusplus
}
#endif
