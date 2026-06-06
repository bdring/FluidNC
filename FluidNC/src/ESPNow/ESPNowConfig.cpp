// Copyright (c) 2026 - Figamore

#include "ESPNowConfig.h"
#include "ESPNowCrypto.h"

#include <esp_log.h>
#include <esp_random.h>
#include <mbedtls/md.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>

#define ESPNOW_NVS_NS "espnow"
#define ESPNOW_KEY_PAIRS "pairs"
#define ESPNOW_KEY_PAIRS_TAG "pairs_tag"
#define ESPNOW_KEY_SECRET "secret"

static constexpr size_t kStorageKeySize = 32;
static constexpr uint8_t kStoredPairingVersion = 2;
static constexpr uint8_t kStoredPairingTagLabel[] = "fluidnc-espnow-pairing-store-auth-v1";
static constexpr uint8_t kStoredPairingWrapLabel[] = "fluidnc-espnow-pairing-store-wrap-v1";

struct StoredPairingList {
    uint8_t version;
    uint8_t count;
    uint8_t reserved[2];
    ESPNowPairingRecord records[ESPNowConfig::MAX_PAIRINGS];
};

static bool allZero(const uint8_t* data, size_t len) {
    uint8_t acc = 0;
    for (size_t i = 0; data && i < len; ++i) {
        acc |= data[i];
    }
    return acc == 0;
}

static bool loadStorageKey(nvs_handle_t h, uint8_t out_key[kStorageKeySize]) {
    size_t len = kStorageKeySize;
    if (nvs_get_blob(h, ESPNOW_KEY_SECRET, out_key, &len) != ESP_OK ||
        len != kStorageKeySize ||
        allZero(out_key, kStorageKeySize)) {
        memset(out_key, 0, kStorageKeySize);
        return false;
    }
    return true;
}

static bool createStorageKey(nvs_handle_t h, uint8_t out_key[kStorageKeySize]) {
    do {
        for (size_t i = 0; i < kStorageKeySize; i += sizeof(uint32_t)) {
            uint32_t word = esp_random();
            memcpy(out_key + i, &word, sizeof(word));
        }
    } while (allZero(out_key, kStorageKeySize));

    esp_err_t err = nvs_set_blob(h, ESPNOW_KEY_SECRET, out_key, kStorageKeySize);
    if (err != ESP_OK) {
        ESP_LOGE("espnow", "storage key NVS write failed: %s", esp_err_to_name(err));
        ESPNowCrypto::secureZero(out_key, kStorageKeySize);
        return false;
    }
    return true;
}

static bool ensureStorageKey(nvs_handle_t h, uint8_t out_key[kStorageKeySize]) {
    return loadStorageKey(h, out_key) || createStorageKey(h, out_key);
}

static void storedPairingTag(const StoredPairingList& list,
                             const uint8_t key[kStorageKeySize],
                             uint8_t out_tag[16]) {
    uint8_t full[32];
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, key, kStorageKeySize);
    mbedtls_md_hmac_update(&ctx, kStoredPairingTagLabel, sizeof(kStoredPairingTagLabel) - 1);
    mbedtls_md_hmac_update(&ctx, reinterpret_cast<const uint8_t*>(&list), sizeof(list));
    mbedtls_md_hmac_finish(&ctx, full);
    mbedtls_md_free(&ctx);

    memcpy(out_tag, full, 16);
    ESPNowCrypto::secureZero(full, sizeof(full));
}

static void storedPairingPad(const uint8_t key[kStorageKeySize],
                             const uint8_t peer_mac[6],
                             uint8_t out_pad[16]) {
    uint8_t full[32];
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, key, kStorageKeySize);
    mbedtls_md_hmac_update(&ctx, kStoredPairingWrapLabel, sizeof(kStoredPairingWrapLabel) - 1);
    mbedtls_md_hmac_update(&ctx, peer_mac, 6);
    mbedtls_md_hmac_finish(&ctx, full);
    mbedtls_md_free(&ctx);

    memcpy(out_pad, full, 16);
    ESPNowCrypto::secureZero(full, sizeof(full));
}

static void cryptStoredLmk(const uint8_t key[kStorageKeySize],
                           const uint8_t peer_mac[6],
                           uint8_t lmk[16]) {
    uint8_t pad[16];
    storedPairingPad(key, peer_mac, pad);
    for (size_t i = 0; i < 16; ++i) {
        lmk[i] ^= pad[i];
    }
    ESPNowCrypto::secureZero(pad, sizeof(pad));
}

static bool verifyStoredPairingTag(nvs_handle_t h, const StoredPairingList& list) {
    uint8_t stored_tag[16] = {};
    uint8_t expected[16] = {};
    uint8_t storage_key[kStorageKeySize] = {};
    size_t len = sizeof(stored_tag);
    bool ok = false;

    if (nvs_get_blob(h, ESPNOW_KEY_PAIRS_TAG, stored_tag, &len) == ESP_OK &&
        len == sizeof(stored_tag) &&
        loadStorageKey(h, storage_key)) {
        storedPairingTag(list, storage_key, expected);
        ok = ESPNowCrypto::constantTimeEquals(expected, stored_tag, sizeof(stored_tag));
    }

    ESPNowCrypto::secureZero(stored_tag, sizeof(stored_tag));
    ESPNowCrypto::secureZero(expected, sizeof(expected));
    ESPNowCrypto::secureZero(storage_key, sizeof(storage_key));
    return ok;
}

static bool loadStoredPairings(nvs_handle_t h, StoredPairingList* out) {
    size_t len = sizeof(*out);
    memset(out, 0, sizeof(*out));
    if (nvs_get_blob(h, ESPNOW_KEY_PAIRS, out, &len) != ESP_OK || len != sizeof(*out)) {
        return false;
    }
    if (out->version != kStoredPairingVersion || out->count > ESPNowConfig::MAX_PAIRINGS) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    if (!verifyStoredPairingTag(h, *out)) {
        ESP_LOGE("espnow", "saved pairing authentication failed");
        memset(out, 0, sizeof(*out));
        return false;
    }
    uint8_t storage_key[kStorageKeySize];
    if (!loadStorageKey(h, storage_key)) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    for (uint8_t i = 0; i < out->count; ++i) {
        cryptStoredLmk(storage_key, out->records[i].peer_mac, out->records[i].lmk);
    }
    ESPNowCrypto::secureZero(storage_key, sizeof(storage_key));
    return true;
}

static void initStoredPairings(StoredPairingList* list) {
    memset(list, 0, sizeof(*list));
    list->version = kStoredPairingVersion;
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
        ESPNowCrypto::secureZero(&list, sizeof(list));
        return 0;
    }

    size_t count = list.count;
    if (count > max_records) {
        count = max_records;
    }
    memcpy(out_records, list.records, count * sizeof(ESPNowPairingRecord));
    ESPNowCrypto::secureZero(&list, sizeof(list));
    return count;
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

    StoredPairingList stored_list;
    memcpy(&stored_list, &list, sizeof(stored_list));

    uint8_t storage_key[kStorageKeySize] = {};
    uint8_t tag[16] = {};
    bool have_storage_key = ensureStorageKey(h, storage_key);
    if (have_storage_key) {
        for (uint8_t i = 0; i < stored_list.count; ++i) {
            cryptStoredLmk(storage_key, stored_list.records[i].peer_mac, stored_list.records[i].lmk);
        }
        storedPairingTag(stored_list, storage_key, tag);
    }

    esp_err_t err = have_storage_key ? nvs_set_blob(h, ESPNOW_KEY_PAIRS, &stored_list, sizeof(stored_list)) : ESP_FAIL;
    if (err == ESP_OK) {
        err = nvs_set_blob(h, ESPNOW_KEY_PAIRS_TAG, tag, sizeof(tag));
    }
    if (err == ESP_OK) err = nvs_commit(h);
    if (err != ESP_OK) {
        ESP_LOGE("espnow", "savePairing NVS write failed: %s", esp_err_to_name(err));
    }
    ESPNowCrypto::secureZero(storage_key, sizeof(storage_key));
    ESPNowCrypto::secureZero(tag, sizeof(tag));
    ESPNowCrypto::secureZero(&stored_list, sizeof(stored_list));
    ESPNowCrypto::secureZero(&list, sizeof(list));
    nvs_close(h);
    return err == ESP_OK;
}

bool ESPNowConfig::removePairing(const uint8_t* peer_mac) {
    if (!peer_mac) {
        return false;
    }

    nvs_handle_t h;
    if (nvs_open(ESPNOW_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }

    StoredPairingList list;
    if (!loadStoredPairings(h, &list)) {
        nvs_close(h);
        return false;
    }

    bool removed = false;
    for (uint8_t i = 0; i < list.count; ++i) {
        if (memcmp(list.records[i].peer_mac, peer_mac, 6) != 0) {
            continue;
        }
        for (uint8_t j = i; j + 1 < list.count; ++j) {
            list.records[j] = list.records[j + 1];
        }
        memset(&list.records[list.count - 1], 0, sizeof(list.records[list.count - 1]));
        list.count--;
        removed = true;
        break;
    }

    esp_err_t err = ESP_OK;
    if (removed) {
        if (list.count == 0) {
            nvs_erase_key(h, ESPNOW_KEY_PAIRS);
            nvs_erase_key(h, ESPNOW_KEY_PAIRS_TAG);
        } else {
            StoredPairingList stored_list;
            memcpy(&stored_list, &list, sizeof(stored_list));

            uint8_t storage_key[kStorageKeySize] = {};
            uint8_t tag[16] = {};
            bool have_storage_key = ensureStorageKey(h, storage_key);
            if (have_storage_key) {
                for (uint8_t i = 0; i < stored_list.count; ++i) {
                    cryptStoredLmk(storage_key, stored_list.records[i].peer_mac, stored_list.records[i].lmk);
                }
                storedPairingTag(stored_list, storage_key, tag);
                err = nvs_set_blob(h, ESPNOW_KEY_PAIRS, &stored_list, sizeof(stored_list));
                if (err == ESP_OK) {
                    err = nvs_set_blob(h, ESPNOW_KEY_PAIRS_TAG, tag, sizeof(tag));
                }
            } else {
                err = ESP_FAIL;
            }
            ESPNowCrypto::secureZero(storage_key, sizeof(storage_key));
            ESPNowCrypto::secureZero(tag, sizeof(tag));
            ESPNowCrypto::secureZero(&stored_list, sizeof(stored_list));
        }
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
    }

    ESPNowCrypto::secureZero(&list, sizeof(list));
    nvs_close(h);
    return removed && err == ESP_OK;
}

bool ESPNowConfig::removePairingIndex(size_t one_based_index, uint8_t removed_mac[6]) {
    if (one_based_index == 0 || one_based_index > MAX_PAIRINGS) {
        return false;
    }

    ESPNowPairingRecord records[MAX_PAIRINGS];
    size_t count = loadPairings(records, MAX_PAIRINGS);
    if (one_based_index > count) {
        ESPNowCrypto::secureZero(records, sizeof(records));
        return false;
    }

    memcpy(removed_mac, records[one_based_index - 1].peer_mac, 6);
    bool ok = removePairing(removed_mac);
    ESPNowCrypto::secureZero(records, sizeof(records));
    return ok;
}

void ESPNowConfig::clearPairing() {
    nvs_handle_t h;
    if (nvs_open(ESPNOW_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_erase_key(h, ESPNOW_KEY_PAIRS);
    nvs_erase_key(h, ESPNOW_KEY_PAIRS_TAG);
    nvs_commit(h);
    nvs_close(h);
}
