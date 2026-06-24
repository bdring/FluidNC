#pragma once

#include <cstdint>
#include <cstddef>

#ifdef WITH_MBEDTLS
#    include <mbedtls/md.h>
#endif

struct SHA256_CTX {
#ifdef WITH_MBEDTLS
    mbedtls_md_context_t ctx;
#else
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    unsigned int datalen;
#endif
};

void sha256_init(SHA256_CTX* ctx);
void sha256_update(SHA256_CTX* ctx, const uint8_t* data, size_t len);
void sha256_final(SHA256_CTX* ctx, uint8_t hash[32]);
