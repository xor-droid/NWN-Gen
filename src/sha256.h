/* SHA-256 (FIPS 180-4). */
#ifndef NWNDGEN_SHA256_H
#define NWNDGEN_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t h[8];
    uint8_t buf[64];
    uint64_t len;
    size_t blen;
} sha256_ctx;

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *data, size_t n);
void sha256_final(sha256_ctx *c, uint8_t out[32]);
void sha256(const void *data, size_t n, uint8_t out[32]);

#endif
