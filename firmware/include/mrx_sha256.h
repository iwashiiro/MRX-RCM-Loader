#ifndef MRX_SHA256_H
#define MRX_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t total_bytes;
    uint8_t block[64];
    size_t block_used;
} mrx_sha256_t;

void mrx_sha256_init(mrx_sha256_t *context);
void mrx_sha256_update(mrx_sha256_t *context, const void *data, size_t size);
void mrx_sha256_final(mrx_sha256_t *context, uint8_t digest[32]);

#endif
