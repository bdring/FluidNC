// Copyright (c) 2026 - Figamore

#pragma once

#include <cstddef>
#include <stdint.h>

struct ESPNowPairingRecord {
    uint8_t peer_mac[6];
    uint8_t lmk[16];
};

class ESPNowConfig {
public:
    static constexpr size_t MAX_PAIRINGS = 8;
    static constexpr uint32_t DEFAULT_REPORT_INTERVAL_MS = 200;

    static size_t loadPairings(ESPNowPairingRecord* out_records, size_t max_records);
    static bool savePairing(const uint8_t* peer_mac, const uint8_t* lmk);
    static bool removePairing(const uint8_t* peer_mac);
    static bool removePairingIndex(size_t one_based_index, uint8_t removed_mac[6]);
    static void clearPairing();
};
