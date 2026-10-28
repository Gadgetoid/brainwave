// SHA-256, used to check firmware images before they are sent to the module.
#include "sha256.h"

namespace {

const uint32_t round_constants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

uint32_t rotate_right(uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

void compress(sha256_context &context, const uint8_t *block) {
    uint32_t schedule[64];
    for (int index = 0; index < 16; index++) {
        schedule[index] = static_cast<uint32_t>(block[index * 4]) << 24 | static_cast<uint32_t>(block[index * 4 + 1]) << 16 |
                          static_cast<uint32_t>(block[index * 4 + 2]) << 8 | block[index * 4 + 3];
    }
    for (int index = 16; index < 64; index++) {
        uint32_t early = schedule[index - 15];
        uint32_t late = schedule[index - 2];
        uint32_t sigma0 = rotate_right(early, 7) ^ rotate_right(early, 18) ^ (early >> 3);
        uint32_t sigma1 = rotate_right(late, 17) ^ rotate_right(late, 19) ^ (late >> 10);
        schedule[index] = schedule[index - 16] + sigma0 + schedule[index - 7] + sigma1;
    }
    uint32_t a = context.state[0];
    uint32_t b = context.state[1];
    uint32_t c = context.state[2];
    uint32_t d = context.state[3];
    uint32_t e = context.state[4];
    uint32_t f = context.state[5];
    uint32_t g = context.state[6];
    uint32_t h = context.state[7];
    for (int index = 0; index < 64; index++) {
        uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t first = h + sum1 + choose + round_constants[index] + schedule[index];
        uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t second = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    context.state[0] += a;
    context.state[1] += b;
    context.state[2] += c;
    context.state[3] += d;
    context.state[4] += e;
    context.state[5] += f;
    context.state[6] += g;
    context.state[7] += h;
}

}

void sha256_start(sha256_context &context) {
    const uint32_t initial[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    for (int index = 0; index < 8; index++) {
        context.state[index] = initial[index];
    }
    context.length = 0;
    context.block_used = 0;
}

void sha256_add(sha256_context &context, const uint8_t *data, size_t length) {
    context.length += length;
    while (length) {
        size_t space = 64 - context.block_used;
        size_t taken = length < space ? length : space;
        for (size_t index = 0; index < taken; index++) {
            context.block[context.block_used + index] = data[index];
        }
        context.block_used += taken;
        data += taken;
        length -= taken;
        if (context.block_used == 64) {
            compress(context, context.block);
            context.block_used = 0;
        }
    }
}

void sha256_finish(sha256_context &context, uint8_t digest[32]) {
    uint64_t bits = context.length * 8;
    uint8_t padding = 0x80;
    sha256_add(context, &padding, 1);
    uint8_t zero = 0;
    while (context.block_used != 56) {
        sha256_add(context, &zero, 1);
    }
    uint8_t length_bytes[8];
    for (int index = 0; index < 8; index++) {
        length_bytes[index] = static_cast<uint8_t>(bits >> (56 - index * 8));
    }
    sha256_add(context, length_bytes, 8);
    for (int index = 0; index < 8; index++) {
        digest[index * 4] = static_cast<uint8_t>(context.state[index] >> 24);
        digest[index * 4 + 1] = static_cast<uint8_t>(context.state[index] >> 16);
        digest[index * 4 + 2] = static_cast<uint8_t>(context.state[index] >> 8);
        digest[index * 4 + 3] = static_cast<uint8_t>(context.state[index]);
    }
}

void sha256_hex(const uint8_t digest[32], char hex[65]) {
    static const char digits[] = "0123456789abcdef";
    for (int index = 0; index < 32; index++) {
        hex[index * 2] = digits[digest[index] >> 4];
        hex[index * 2 + 1] = digits[digest[index] & 15];
    }
    hex[64] = 0;
}
