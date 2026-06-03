// Copyright (c) 2026 - Figamore

#include "ESPNowConfig.h"
#include "../Configuration/HandlerBase.h"

#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>

#define ESPNOW_NVS_NS "espnow"
#define ESPNOW_KEY_PAIRS "pairs"

struct StoredPairingList {
    uint8_t version;
    uint8_t count;
    uint8_t reserved[2];
    ESPNowPairingRecord records[ESPNowConfig::MAX_PAIRINGS];
};

static bool loadStoredPairings(nvs_handle_t h, StoredPairingList* out) {
    size_t len = sizeof(*out);
    memset(out, 0, sizeof(*out));
    if (nvs_get_blob(h, ESPNOW_KEY_PAIRS, out, &len) != ESP_OK || len != sizeof(*out)) {
        return false;
    }
    if (out->version != 1 || out->count > ESPNowConfig::MAX_PAIRINGS) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    return true;
}

static void initStoredPairings(StoredPairingList* list) {
    memset(list, 0, sizeof(*list));
    list->version = 1;
}

void ESPNowConfig::group(Configuration::HandlerBase& handler) {
    handler.item("name", _name);
    handler.item("pair_code", _pair_code);
    handler.item("report_interval_ms", _report_interval_ms, 20, 5000);
}

std::string ESPNowConfig::displayName() const {
    if (!_name.empty()) {
        return _name;
    }
    return std::string("espnow") + std::to_string(_slot);
}

bool ESPNowConfig::loadPairing(uint8_t* peer_mac_out, uint8_t* lmk_out) {
    ESPNowPairingRecord records[MAX_PAIRINGS];
    if (loadPairings(records, MAX_PAIRINGS) == 0) {
        return false;
    }
    memcpy(peer_mac_out, records[0].peer_mac, 6);
    memcpy(lmk_out, records[0].lmk, 16);
    return true;
}

size_t ESPNowConfig::loadPairings(ESPNowPairingRecord* out_records, size_t max_records) {
    nvs_handle_t h;
    if (nvs_open(ESPNOW_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return 0;
    }

    StoredPairingList list;
    bool ok = loadStoredPairings(h, &list);
    nvs_close(h);

    if (!ok || !out_records || max_records == 0) {
        return 0;
    }

    size_t count = list.count;
    if (count > max_records) {
        count = max_records;
    }
    memcpy(out_records, list.records, count * sizeof(ESPNowPairingRecord));
    return count;
}

bool ESPNowConfig::findPairing(const uint8_t* peer_mac, uint8_t* lmk_out) {
    if (!peer_mac || !lmk_out) {
        return false;
    }

    ESPNowPairingRecord records[MAX_PAIRINGS];
    size_t count = loadPairings(records, MAX_PAIRINGS);
    for (size_t i = 0; i < count; ++i) {
        if (memcmp(records[i].peer_mac, peer_mac, 6) == 0) {
            memcpy(lmk_out, records[i].lmk, 16);
            return true;
        }
    }
    return false;
}

bool ESPNowConfig::savePairing(const uint8_t* peer_mac, const uint8_t* lmk) {
    if (!peer_mac || !lmk) {
        return false;
    }

    nvs_handle_t h;
    if (nvs_open(ESPNOW_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }

    StoredPairingList list;
    if (!loadStoredPairings(h, &list)) {
        initStoredPairings(&list);
    }

    size_t slot = list.count;
    for (size_t i = 0; i < list.count; ++i) {
        if (memcmp(list.records[i].peer_mac, peer_mac, 6) == 0) {
            slot = i;
            break;
        }
    }
    if (slot >= MAX_PAIRINGS) {
        slot = MAX_PAIRINGS - 1;
    }

    memcpy(list.records[slot].peer_mac, peer_mac, 6);
    memcpy(list.records[slot].lmk, lmk, 16);
    if (slot == list.count && list.count < MAX_PAIRINGS) {
        list.count++;
    }

    esp_err_t err = nvs_set_blob(h, ESPNOW_KEY_PAIRS, &list, sizeof(list));
    if (err == ESP_OK) err = nvs_commit(h);
    if (err != ESP_OK) {
        ESP_LOGE("espnow", "savePairing NVS write failed: %s", esp_err_to_name(err));
    }
    nvs_close(h);
    return err == ESP_OK;
}

void ESPNowConfig::clearPairing() {
    nvs_handle_t h;
    if (nvs_open(ESPNOW_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_erase_key(h, ESPNOW_KEY_PAIRS);
    nvs_commit(h);
    nvs_close(h);
}

bool ESPNowConfig::hasPairing() {
    uint8_t mac[6], lmk[16];
    return loadPairing(mac, lmk);
}
