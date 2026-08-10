#include "Platform.h"
#include "SHA256.h"

#ifdef WITH_MBEDTLS
#    include <mbedtls/md.h>
#else
#    include <cstring>

// clang-format off
static const uint32_t k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};
// clang-format on

static inline uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

static inline uint32_t choose(uint32_t e, uint32_t f, uint32_t g) {
    return (e & f) ^ (~e & g);
}

static inline uint32_t majority(uint32_t a, uint32_t b, uint32_t c) {
    return (a & b) ^ (a & c) ^ (b & c);
}

static inline uint32_t sig0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

static inline uint32_t sig1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

static inline uint32_t theta0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

static inline uint32_t theta1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

static void sha256_transform(SHA256_CTX* ctx, const uint8_t data[]) {
    uint32_t m[64];
    for (unsigned int i = 0; i < 16; ++i) {
        m[i] = (uint32_t)data[i * 4] << 24 |
               (uint32_t)data[i * 4 + 1] << 16 |
               (uint32_t)data[i * 4 + 2] << 8 |
               (uint32_t)data[i * 4 + 3];
    }
    for (unsigned int i = 16; i < 64; ++i) {
        m[i] = theta1(m[i - 2]) + m[i - 7] + theta0(m[i - 15]) + m[i - 16];
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (unsigned int i = 0; i < 64; ++i) {
        uint32_t t1 = h + sig1(e) + choose(e, f, g) + k[i] + m[i];
        uint32_t t2 = sig0(a) + majority(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}
#endif

void sha256_init(SHA256_CTX* ctx) {
#ifdef WITH_MBEDTLS
    mbedtls_md_init(&ctx->ctx);
    mbedtls_md_setup(&ctx->ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    mbedtls_md_starts(&ctx->ctx);
#else
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
#endif
}

void sha256_update(SHA256_CTX* ctx, const uint8_t* data, size_t len) {
#ifdef WITH_MBEDTLS
    mbedtls_md_update(&ctx->ctx, data, len);
#else
    for (size_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
#endif
}

void sha256_final(SHA256_CTX* ctx, uint8_t hash[32]) {
#ifdef WITH_MBEDTLS
    mbedtls_md_finish(&ctx->ctx, hash);
    mbedtls_md_free(&ctx->ctx);
#else
    unsigned int i = ctx->datalen;

    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) {
            ctx->data[i++] = 0x00;
        }
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) {
            ctx->data[i++] = 0x00;
        }
        sha256_transform(ctx, ctx->data);
        std::memset(ctx->data, 0, 56);
    }

    ctx->bitlen += static_cast<uint64_t>(ctx->datalen) * 8;
    ctx->data[63] = static_cast<uint8_t>(ctx->bitlen);
    ctx->data[62] = static_cast<uint8_t>(ctx->bitlen >> 8);
    ctx->data[61] = static_cast<uint8_t>(ctx->bitlen >> 16);
    ctx->data[60] = static_cast<uint8_t>(ctx->bitlen >> 24);
    ctx->data[59] = static_cast<uint8_t>(ctx->bitlen >> 32);
    ctx->data[58] = static_cast<uint8_t>(ctx->bitlen >> 40);
    ctx->data[57] = static_cast<uint8_t>(ctx->bitlen >> 48);
    ctx->data[56] = static_cast<uint8_t>(ctx->bitlen >> 56);

    sha256_transform(ctx, ctx->data);

    for (unsigned int j = 0; j < 4; ++j) {
        for (unsigned int i = 0; i < 8; ++i) {
            hash[j + (i * 4)] = static_cast<uint8_t>((ctx->state[i] >> (24 - j * 8)) & 0xFF);
        }
    }
#endif
}
