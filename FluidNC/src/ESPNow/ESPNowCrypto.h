// 2026 - Figamore

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <atomic>

namespace ESPNowCrypto {

struct ReplayState {
    uint32_t win_nonce = 0;
    uint32_t win_top = 0;
    uint64_t win_bitmap = 0;
    uint32_t underflow = 0;
    uint32_t regen_ms = 0;
};

void deriveLmk(const char* code, uint8_t out_lmk[16]);
void derivePmk(uint8_t out_pmk[16]);
void pairingAuthTag(const uint8_t key[16], const uint8_t* data, size_t len, uint8_t out[16]);
bool verifyPairingAuthTag(const uint8_t key[16], const uint8_t* data, size_t len, const uint8_t tag[16]);
uint32_t randomNonce();
void issueRxChallenge(std::atomic<uint32_t>& rx_nonce);
bool stampAntiReplayTag(std::atomic<bool>& tx_peer_known,
                        std::atomic<uint32_t>& tx_peer_nonce,
                        std::atomic<uint32_t>& tx_counter,
                        uint8_t tag[8]);
bool acceptReplay(std::atomic<uint32_t>& rx_nonce,
                  ReplayState& replay,
                  uint32_t nonce,
                  uint32_t counter,
                  uint32_t now_ms);

}