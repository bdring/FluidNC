// Copyright (c) 2026 - Figamore

#pragma once

#include "../Configuration/Configurable.h"
#include <string>
#include <stdint.h>

class ESPNowConfig : public Configuration::Configurable {
public:
    std::string _pair_code = "";

    void group(Configuration::HandlerBase& handler) override;

    static bool loadPairing(uint8_t* peer_mac_out, uint8_t* lmk_out);
    static void savePairing(const uint8_t* peer_mac, const uint8_t* lmk);
    static void clearPairing();
    static bool hasPairing();
};
