// sha256.h — SHA-256 hash implementation for VernisOS
//
// Phase 13: Pure C SHA-256 for user password authentication.

#ifndef VERNISOS_SHA256_H
#define VERNISOS_SHA256_H

#include <stdint.h>
#include <stddef.h>

#define SHA256_BLOCK_SIZE  64
#define SHA256_DIGEST_SIZE 32

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t  buffer[SHA256_BLOCK_SIZE];
    uint32_t buf_len;
} Sha256Context;

// Initialize a SHA-256 context
void sha256_init(Sha256Context *ctx);

// Feed data into the hash
void sha256_update(Sha256Context *ctx, const uint8_t *data, size_t len);

// Finalize and produce the 32-byte digest
void sha256_final(Sha256Context *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);

// Convenience: hash a buffer in one call
void sha256_hash(const uint8_t *data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE]);

// Compare two digests (constant-time)
int sha256_compare(const uint8_t a[SHA256_DIGEST_SIZE],
                   const uint8_t b[SHA256_DIGEST_SIZE]);

#endif // VERNISOS_SHA256_H
