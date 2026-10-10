// Copyright (c) 2026 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

extern "C" {
// Core dump: forced in by "-u esp_system_include_coredump_init", but min_littlefs.csv has no coredump partition.
void esp_system_include_coredump_init(void) {}
// Called from the panic handler (libesp_system panic.c); panic_info_t* passed as void*.
void esp_core_dump_write(void* info) {}

// Wi-Fi provisioning: Arduino WiFiGeneric calls network_prov_mgr_deinit() on teardown;
// FluidNC never initializes the provisioning manager, so deinit is a no-op anyway.
void network_prov_mgr_deinit(void) {}
}

// Arduino chip debug report: only printed when ARDUHAL_LOG_LEVEL >= DEBUG or a sketch
// overrides shouldPrintChipDebugReport(); FluidNC builds with CORE_DEBUG_LEVEL=0.
void printBeforeSetupInfo(void) {}
void printAfterSetupInfo(void) {}
