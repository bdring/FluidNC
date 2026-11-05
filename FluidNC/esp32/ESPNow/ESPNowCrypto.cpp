// 2026 - Figamore

#include "ESPNowCrypto.h"

#include <esp_idf_version.h>
#include <esp_random.h>
#include <string.h>

// IDF 6 ships Mbed TLS 4 / TF-PSA-Crypto, which removed the legacy ECDH, SHA-256
// and HMAC APIs; use PSA Crypto there.  Older IDFs keep the legacy calls.
#if ESP_IDF_VERSION_MAJOR >= 6
#    define ESPNOW_USE_PSA 1
#    include <psa/crypto.h>
#else
#    define ESPNOW_USE_PSA 0
#    include <mbedtls/ecdh.h>
#    include <mbedtls/ecp.h>
#    include <mbedtls/md.h>
#    include <mbedtls/sha256.h>
#endif

namespace {

constexpr uint32_t kReplayRegenMs = 3000;
constexpr uint32_t kReplayUnderflowLimit = 8;
constexpr char kEspNowPmkLabel[] = "fluiddial-espnow-pmk-v1";
constexpr char kPairingWindowLabel[] = "fluidnc-espnow-pairing-window-v1";
constexpr char kPairingSessionLabel[] = "fluidnc-espnow-pairing-session-v1";

#if !ESPNOW_USE_PSA
int espRandomBytes(void*, unsigned char* out, size_t len) {
    while (len > 0) {
        uint32_t word = esp_random();
        size_t chunk = len < sizeof(word) ? len : sizeof(word);
        memcpy(out, &word, chunk);
        out += chunk;
        len -= chunk;
    }
    return 0;
}

#endif

}  // namespace

namespace ESPNowCrypto {

void hmacSha256(const uint8_t* key, size_t key_len, std::initializer_list<HmacPart> parts, uint8_t out[32]) {
#if ESPNOW_USE_PSA
    bool                  ok   = false;
    mbedtls_svc_key_id_t  id   = MBEDTLS_SVC_KEY_ID_INIT;
    psa_key_attributes_t  attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_mac_operation_t   op   = PSA_MAC_OPERATION_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attr, PSA_BYTES_TO_BITS(key_len));
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attr, PSA_ALG_HMAC(PSA_ALG_SHA_256));

    if (psa_crypto_init() == PSA_SUCCESS && psa_import_key(&attr, key, key_len, &id) == PSA_SUCCESS &&
        psa_mac_sign_setup(&op, id, PSA_ALG_HMAC(PSA_ALG_SHA_256)) == PSA_SUCCESS) {
        ok = true;
        for (const auto& part : parts) {
            if (part.data && part.len > 0 &&
                psa_mac_update(&op, reinterpret_cast<const uint8_t*>(part.data), part.len) != PSA_SUCCESS) {
                ok = false;
                break;
            }
        }
        size_t mac_len = 0;
        ok             = ok && psa_mac_sign_finish(&op, out, 32, &mac_len) == PSA_SUCCESS && mac_len == 32;
    }
    psa_mac_abort(&op);
    psa_destroy_key(id);
    psa_reset_key_attributes(&attr);
    if (!ok) {
        secureZero(out, 32);
    }
#else
    mbedtls_md_context_t     ctx;
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    mbedtls_md_init(&ctx);
    bool ok = mbedtls_md_setup(&ctx, info, 1) == 0 && mbedtls_md_hmac_starts(&ctx, key, key_len) == 0;
    for (const auto& part : parts) {
        if (ok && part.data && part.len > 0) {
            ok = mbedtls_md_hmac_update(&ctx, reinterpret_cast<const uint8_t*>(part.data), part.len) == 0;
        }
    }
    ok = ok && mbedtls_md_hmac_finish(&ctx, out) == 0;
    mbedtls_md_free(&ctx);
    if (!ok) {
        secureZero(out, 32);
    }
#endif
}

static void sha256Label(const char* label, uint8_t out[16]) {
    uint8_t hash[32];
#if ESPNOW_USE_PSA
    size_t hash_len = 0;
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_compute(PSA_ALG_SHA_256, reinterpret_cast<const uint8_t*>(label), strlen(label), hash, sizeof(hash), &hash_len) !=
            PSA_SUCCESS ||
        hash_len != sizeof(hash)) {
        secureZero(hash, sizeof(hash));
    }
#else
    mbedtls_sha256(reinterpret_cast<const uint8_t*>(label), strlen(label), hash, 0);
#endif
    memcpy(out, hash, 16);
    secureZero(hash, sizeof(hash));
}

void derivePairingWindowLmk(uint8_t out_lmk[16]) {
    sha256Label(kPairingWindowLabel, out_lmk);
}

void derivePmk(uint8_t out_pmk[16]) {
    sha256Label(kEspNowPmkLabel, out_pmk);
}

bool generateEcdhKeypair(uint8_t private_key[ECDH_PRIVATE_KEY_SIZE], uint8_t public_key[ECDH_PUBLIC_KEY_SIZE]) {
    if (!private_key || !public_key) {
        return false;
    }

#if ESPNOW_USE_PSA
    bool                 ok          = false;
    size_t               private_len = 0;
    size_t               public_len  = 0;
    mbedtls_svc_key_id_t id          = MBEDTLS_SVC_KEY_ID_INIT;
    psa_key_attributes_t attr        = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, 256);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_DERIVE | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attr, PSA_ALG_ECDH);

    if (psa_crypto_init() == PSA_SUCCESS && psa_generate_key(&attr, &id) == PSA_SUCCESS &&
        psa_export_key(id, private_key, ECDH_PRIVATE_KEY_SIZE, &private_len) == PSA_SUCCESS &&
        private_len == ECDH_PRIVATE_KEY_SIZE &&
        psa_export_public_key(id, public_key, ECDH_PUBLIC_KEY_SIZE, &public_len) == PSA_SUCCESS &&
        public_len == ECDH_PUBLIC_KEY_SIZE) {
        ok = true;
    }

    psa_destroy_key(id);
    psa_reset_key_attributes(&attr);
    if (!ok) {
        secureZero(private_key, ECDH_PRIVATE_KEY_SIZE);
        secureZero(public_key, ECDH_PUBLIC_KEY_SIZE);
    }
    return ok;
#else
    bool ok = false;
    size_t public_len = 0;
    mbedtls_ecp_group group;
    mbedtls_mpi private_mpi;
    mbedtls_ecp_point public_point;

    mbedtls_ecp_group_init(&group);
    mbedtls_mpi_init(&private_mpi);
    mbedtls_ecp_point_init(&public_point);

    if (mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_ecdh_gen_public(&group, &private_mpi, &public_point, espRandomBytes, nullptr) == 0 &&
        mbedtls_mpi_write_binary(&private_mpi, private_key, ECDH_PRIVATE_KEY_SIZE) == 0 &&
        mbedtls_ecp_point_write_binary(&group,
                                       &public_point,
                                       MBEDTLS_ECP_PF_UNCOMPRESSED,
                                       &public_len,
                                       public_key,
                                       ECDH_PUBLIC_KEY_SIZE) == 0 &&
        public_len == ECDH_PUBLIC_KEY_SIZE) {
        ok = true;
    }

    if (!ok) {
        secureZero(private_key, ECDH_PRIVATE_KEY_SIZE);
        secureZero(public_key, ECDH_PUBLIC_KEY_SIZE);
    }
    mbedtls_ecp_point_free(&public_point);
    mbedtls_mpi_free(&private_mpi);
    mbedtls_ecp_group_free(&group);
    return ok;
#endif
}

bool deriveEcdhSharedSecret(const uint8_t private_key[ECDH_PRIVATE_KEY_SIZE],
                            const uint8_t peer_public_key[ECDH_PUBLIC_KEY_SIZE],
                            uint8_t out_secret[ECDH_SHARED_SECRET_SIZE]) {
    if (!private_key || !peer_public_key || !out_secret || peer_public_key[0] != 0x04) {
        return false;
    }

#if ESPNOW_USE_PSA
    // psa_raw_key_agreement() rejects a peer point that is not on the curve,
    // which covers what mbedtls_ecp_check_pubkey() did on the legacy path.
    bool                 ok         = false;
    size_t               secret_len = 0;
    mbedtls_svc_key_id_t id         = MBEDTLS_SVC_KEY_ID_INIT;
    psa_key_attributes_t attr       = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, 256);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attr, PSA_ALG_ECDH);

    if (psa_crypto_init() == PSA_SUCCESS && psa_import_key(&attr, private_key, ECDH_PRIVATE_KEY_SIZE, &id) == PSA_SUCCESS &&
        psa_raw_key_agreement(PSA_ALG_ECDH, id, peer_public_key, ECDH_PUBLIC_KEY_SIZE, out_secret, ECDH_SHARED_SECRET_SIZE, &secret_len) ==
            PSA_SUCCESS &&
        secret_len == ECDH_SHARED_SECRET_SIZE) {
        ok = true;
    }

    psa_destroy_key(id);
    psa_reset_key_attributes(&attr);
    if (!ok) {
        secureZero(out_secret, ECDH_SHARED_SECRET_SIZE);
    }
    return ok;
#else
    bool ok = false;
    mbedtls_ecp_group group;
    mbedtls_mpi private_mpi;
    mbedtls_mpi shared_mpi;
    mbedtls_ecp_point peer_point;

    mbedtls_ecp_group_init(&group);
    mbedtls_mpi_init(&private_mpi);
    mbedtls_mpi_init(&shared_mpi);
    mbedtls_ecp_point_init(&peer_point);

    if (mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_mpi_read_binary(&private_mpi, private_key, ECDH_PRIVATE_KEY_SIZE) == 0 &&
        mbedtls_ecp_point_read_binary(&group, &peer_point, peer_public_key, ECDH_PUBLIC_KEY_SIZE) == 0 &&
        mbedtls_ecp_check_pubkey(&group, &peer_point) == 0 &&
        mbedtls_ecdh_compute_shared(&group, &shared_mpi, &peer_point, &private_mpi, espRandomBytes, nullptr) == 0 &&
        mbedtls_mpi_write_binary(&shared_mpi, out_secret, ECDH_SHARED_SECRET_SIZE) == 0) {
        ok = true;
    }

    if (!ok) {
        secureZero(out_secret, ECDH_SHARED_SECRET_SIZE);
    }
    mbedtls_ecp_point_free(&peer_point);
    mbedtls_mpi_free(&shared_mpi);
    mbedtls_mpi_free(&private_mpi);
    mbedtls_ecp_group_free(&group);
    return ok;
#endif
}

void derivePairingSessionLmk(const uint8_t pairing_window_lmk[16],
                             const uint8_t shared_secret[ECDH_SHARED_SECRET_SIZE],
                             const uint8_t* first_transcript,
                             size_t first_len,
                             const uint8_t* second_transcript,
                             size_t second_len,
                             uint8_t out_lmk[16]) {
    uint8_t full[32];
    hmacSha256(pairing_window_lmk,
               16,
               { { kPairingSessionLabel, strlen(kPairingSessionLabel) },
                 { shared_secret, ECDH_SHARED_SECRET_SIZE },
                 { first_transcript, first_len },
                 { second_transcript, second_len } },
               full);
    memcpy(out_lmk, full, 16);
    secureZero(full, sizeof(full));
}

void pairingAuthTag(const uint8_t key[16], const uint8_t* data, size_t len, uint8_t out[16]) {
    uint8_t full[32];
    hmacSha256(key, 16, { { data, len } }, full);
    memcpy(out, full, 16);
    secureZero(full, sizeof(full));
}

bool verifyPairingAuthTag(const uint8_t key[16], const uint8_t* data, size_t len, const uint8_t tag[16]) {
    uint8_t expected[16];
    pairingAuthTag(key, data, len, expected);
    bool ok = constantTimeEquals(expected, tag, 16);
    secureZero(expected, sizeof(expected));
    return ok;
}

bool constantTimeEquals(const uint8_t* a, const uint8_t* b, size_t len) {
    if (!a || !b) {
        return false;
    }
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

void secureZero(void* data, size_t len) {
    volatile uint8_t* p = reinterpret_cast<volatile uint8_t*>(data);
    while (p && len-- > 0) {
        *p++ = 0;
    }
}

uint32_t randomNonce() {
    uint32_t nonce;
    do {
        nonce = esp_random();
    } while (nonce == 0);
    return nonce;
}

void issueRxChallenge(std::atomic<uint32_t>& rx_nonce) {
    rx_nonce.store(randomNonce(), std::memory_order_release);
}

bool stampAntiReplayTag(std::atomic<bool>& tx_peer_known,
                        std::atomic<uint32_t>& tx_peer_nonce,
                        std::atomic<uint32_t>& tx_counter,
                        uint8_t tag[8]) {
    if (!tx_peer_known.load(std::memory_order_acquire)) {
        return false;
    }
    uint32_t nonce = tx_peer_nonce.load(std::memory_order_acquire);
    uint32_t counter = tx_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    memcpy(tag, &nonce, 4);
    memcpy(tag + 4, &counter, 4);
    return true;
}

bool acceptReplay(std::atomic<uint32_t>& rx_nonce,
                  ReplayState& replay,
                  uint32_t nonce,
                  uint32_t counter,
                  uint32_t now_ms) {
    uint32_t current = rx_nonce.load(std::memory_order_acquire);
    if (current != replay.win_nonce) {
        replay.win_nonce = current;
        replay.win_top = 0;
        replay.win_bitmap = 0;
        replay.underflow = 0;
    }
    if (nonce != current || counter == 0) {
        return false;
    }

    if (counter > replay.win_top) {
        uint32_t shift = counter - replay.win_top;
        replay.win_bitmap = (shift >= 64) ? 0ULL : (replay.win_bitmap << shift);
        replay.win_bitmap |= 1ULL;
        replay.win_top = counter;
        replay.underflow = 0;
        return true;
    }

    uint32_t diff = replay.win_top - counter;
    if (diff < 64) {
        uint64_t mask = 1ULL << diff;
        if (replay.win_bitmap & mask) {
            return false;
        }
        replay.win_bitmap |= mask;
        return true;
    }

    if (++replay.underflow >= kReplayUnderflowLimit) {
        if ((uint32_t)(now_ms - replay.regen_ms) > kReplayRegenMs) {
            replay.regen_ms = now_ms;
            issueRxChallenge(rx_nonce);
        }
        replay.underflow = 0;
    }
    return false;
}

}  // namespace ESPNowCrypto
