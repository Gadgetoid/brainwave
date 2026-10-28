// SHA-256, used to check firmware images before they are sent to the module.
#pragma once

#include <stddef.h>
#include <stdint.h>

struct sha256_context {
    uint32_t state[8];
    uint64_t length;
    uint8_t block[64];
    size_t block_used;
};

void sha256_start(sha256_context &context);
void sha256_add(sha256_context &context, const uint8_t *data, size_t length);
void sha256_finish(sha256_context &context, uint8_t digest[32]);
void sha256_hex(const uint8_t digest[32], char hex[65]);
