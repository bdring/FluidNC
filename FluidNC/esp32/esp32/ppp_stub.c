// Copyright (c) 2026 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// The prebuilt liblwip.a / libesp_netif.a are compiled with
// LWIP_PPP_SUPPORT=y, which drags the whole lwIP PPP stack into the image even
// though FluidNC never creates a PPP interface.  Two chains pull it in:
//
//   lwip_init() -> ppp_init()                        (liblwip.a init.c)
//   esp_netif_lwip.c -> esp_netif_*_ppp*()           (libesp_netif.a)
//       -> esp_netif_lwip_ppp.c -> ppp_set_auth() -> liblwip.a ppp.c -> ...
//
// As direct link inputs, these definitions are bound before the archives are
// searched, so neither esp_netif_lwip_ppp.c.obj nor lwIP's ppp.c.obj (and
// everything they reference) is pulled in.
//
// Signatures match components/esp_netif/lwip/esp_netif_lwip_ppp.h at the
// esp-idf v5.5.4 tag.  netif_related_data_t and esp_netif_auth_type_t are
// private / enum types; void* and int are ABI-identical stand-ins.
// esp_netif_recv_ret_t is void because CONFIG_ESP_NETIF_RECEIVE_REPORT_ERRORS
// is not set in this SDK.  Re-check all of this if the IDF version changes.

#include <stddef.h>
#include "esp_err.h"

void ppp_init(void) {}

void* esp_netif_new_ppp(void* esp_netif, const void* esp_netif_stack_config) {
    return NULL;
}

esp_err_t esp_netif_start_ppp(void* esp_netif) {
    return ESP_ERR_NOT_SUPPORTED;
}

void esp_netif_lwip_ppp_input(void* ppp, void* buffer, size_t len, void* eb) {}

void esp_netif_destroy_ppp(void* netif_related) {}

esp_err_t esp_netif_stop_ppp(void* netif_related) {
    return ESP_ERR_NOT_SUPPORTED;
}

void esp_netif_ppp_set_default_netif(void* netif_related) {}

esp_err_t esp_netif_ppp_set_auth_internal(void* netif, int authtype, const char* user, const char* passwd) {
    return ESP_ERR_NOT_SUPPORTED;
}
