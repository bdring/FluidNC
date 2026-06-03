// Copyright (c) 2026 - Figamore

#pragma once

#include "../Configuration/Configurable.h"
#include <cstddef>
#include <string>
#include <stdint.h>

struct ESPNowPairingRecord {
    uint8_t peer_mac[6];
    uint8_t lmk[16];
};

class ESPNowConfig : public Configuration::Configurable {
public:
    static constexpr size_t MAX_CONFIGS = 5;
    static constexpr size_t MAX_PAIRINGS = 8;
    static constexpr uint32_t DEFAULT_REPORT_INTERVAL_MS = 200;

    explicit ESPNowConfig(uint8_t slot = 0) : _slot(slot) {}

    std::string _name = "";
    std::string _pair_code = "";
    uint32_t    _report_interval_ms = DEFAULT_REPORT_INTERVAL_MS;

    void group(Configuration::HandlerBase& handler) override;

    bool        configured() const { return !_pair_code.empty(); }
    std::string displayName() const;

    static bool loadPairing(uint8_t* peer_mac_out, uint8_t* lmk_out);
    static size_t loadPairings(ESPNowPairingRecord* out_records, size_t max_records);
    static bool findPairing(const uint8_t* peer_mac, uint8_t* lmk_out);
    static bool savePairing(const uint8_t* peer_mac, const uint8_t* lmk);
    static void clearPairing();
    static bool hasPairing();

private:
    uint8_t _slot = 0;
};
