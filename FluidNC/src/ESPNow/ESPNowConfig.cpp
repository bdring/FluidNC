// Copyright (c) 2026 - Figamore

#include "ESPNowConfig.h"
#include "../Configuration/HandlerBase.h"

#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>

#define ESPNOW_NVS_NS "espnow"
#define ESPNOW_KEY_MAC "peer_mac"
#define ESPNOW_KEY_LMK "peer_lmk"

void ESPNowConfig::group(Configuration::HandlerBase& handler) {
    handler.item("pair_code", _pair_code);
}

bool ESPNowConfig::loadPairing(uint8_t* peer_mac_out, uint8_t* lmk_out) {
    nvs_handle_t h;
    if (nvs_open(ESPNOW_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t mac_len = 6;
    size_t lmk_len = 16;
    bool   ok      = (nvs_get_blob(h, ESPNOW_KEY_MAC, peer_mac_out, &mac_len) == ESP_OK) &&
                     (nvs_get_blob(h, ESPNOW_KEY_LMK, lmk_out, &lmk_len) == ESP_OK) &&
                     mac_len == 6 && lmk_len == 16;
    nvs_close(h);
    return ok;
}

void ESPNowConfig::savePairing(const uint8_t* peer_mac, const uint8_t* lmk) {
    nvs_handle_t h;
    if (nvs_open(ESPNOW_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    esp_err_t err = nvs_set_blob(h, ESPNOW_KEY_MAC, peer_mac, 6);
    if (err == ESP_OK) err = nvs_set_blob(h, ESPNOW_KEY_LMK, lmk, 16);
    if (err == ESP_OK) err = nvs_commit(h);
    if (err != ESP_OK) {
        ESP_LOGE("espnow", "savePairing NVS write failed: %s", esp_err_to_name(err));
    }
    nvs_close(h);
}

void ESPNowConfig::clearPairing() {
    nvs_handle_t h;
    if (nvs_open(ESPNOW_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_erase_key(h, ESPNOW_KEY_MAC);
    nvs_erase_key(h, ESPNOW_KEY_LMK);
    nvs_commit(h);
    nvs_close(h);
}

bool ESPNowConfig::hasPairing() {
    uint8_t mac[6], lmk[16];
    return loadPairing(mac, lmk);
}
