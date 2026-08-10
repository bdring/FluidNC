// 2026 - Figamore

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <atomic>

namespace ESPNowCrypto {

static constexpr size_t ECDH_PRIVATE_KEY_SIZE = 32;
static constexpr size_t ECDH_PUBLIC_KEY_SIZE  = 65;
static constexpr size_t ECDH_SHARED_SECRET_SIZE = 32;

struct ReplayState {
    uint32_t win_nonce = 0;
    uint32_t win_top = 0;
    uint64_t win_bitmap = 0;
    uint32_t underflow = 0;
    uint32_t regen_ms = 0;
};

void derivePairingWindowLmk(uint8_t out_lmk[16]);
void derivePmk(uint8_t out_pmk[16]);
bool generateEcdhKeypair(uint8_t private_key[ECDH_PRIVATE_KEY_SIZE], uint8_t public_key[ECDH_PUBLIC_KEY_SIZE]);
bool deriveEcdhSharedSecret(const uint8_t private_key[ECDH_PRIVATE_KEY_SIZE],
                            const uint8_t peer_public_key[ECDH_PUBLIC_KEY_SIZE],
                            uint8_t out_secret[ECDH_SHARED_SECRET_SIZE]);
void derivePairingSessionLmk(const uint8_t pairing_window_lmk[16],
                             const uint8_t shared_secret[ECDH_SHARED_SECRET_SIZE],
                             const uint8_t* first_transcript,
                             size_t first_len,
                             const uint8_t* second_transcript,
                             size_t second_len,
                             uint8_t out_lmk[16]);
void pairingAuthTag(const uint8_t key[16], const uint8_t* data, size_t len, uint8_t out[16]);
bool verifyPairingAuthTag(const uint8_t key[16], const uint8_t* data, size_t len, const uint8_t tag[16]);
bool constantTimeEquals(const uint8_t* a, const uint8_t* b, size_t len);
void secureZero(void* data, size_t len);
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
