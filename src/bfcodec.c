/*
 * Blowfish codec (custom P/S from pi) — encrypt/decrypt, CBC.
 * Matches decrypt_info.py: F = (S0[a]+S1[b]) ^ (S2[c]+S3[d]), BE block I/O.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "bfcodec.h"

struct C_BLOWFISH {
    /** @cond INTERNAL_HIDDEN */
    uint32_t p[18];
    uint32_t s[4][256];
    /** @endcond */
};

#define BF_INIT_BYTES_LEN (18 * 4 + 4 * 256 * 4)

static const unsigned char BF_INIT_BYTES[] = {
#include "bf_init_bytes.inc"
};

static_assert(sizeof(BF_INIT_BYTES) == BF_INIT_BYTES_LEN, "BF_INIT_BYTES size");

/* Read big-endian u32 from the 4 bytes at p. */
static inline uint32_t read_u32_be(const uint8_t p[static 4]) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* Write big-endian u32 into b[0..3]. */
static void write_u32_be(uint8_t b[static 4], uint32_t x) {
    b[0] = (uint8_t)(x >> 24);
    b[1] = (uint8_t)(x >> 16);
    b[2] = (uint8_t)(x >> 8);
    b[3] = (uint8_t)x;
}

static uint32_t bf_f(C_BLOWFISH *blf, uint32_t x) {
    uint32_t a = (x >> 24) & 0xff;
    uint32_t b = (x >> 16) & 0xff;
    uint32_t c = (x >> 8) & 0xff;
    uint32_t d = x & 0xff;
    uint32_t t = (blf->s[0][a] + blf->s[1][b]) ^ (blf->s[2][c] + blf->s[3][d]);
    return t;
}

static void bf_encrypt_block(C_BLOWFISH *blf, uint32_t *L, uint32_t *R) {
    uint32_t l = *L, r = *R;
    for (int i = 0; i < 16; i += 2) {
        l ^= blf->p[i];
        r ^= bf_f(blf, l);
        r ^= blf->p[i + 1];
        l ^= bf_f(blf, r);
    }
    l ^= blf->p[16];
    r ^= blf->p[17];
    *L = r;
    *R = l;
}

static void bf_decrypt_block(C_BLOWFISH *blf, uint32_t *L, uint32_t *R) {
    uint32_t l = *L, r = *R;
    for (int i = 16; i >= 2; i -= 2) {
        l ^= blf->p[i + 1];
        r ^= bf_f(blf, l);
        r ^= blf->p[i];
        l ^= bf_f(blf, r);
    }
    l ^= blf->p[1];
    r ^= blf->p[0];
    *L = r;
    *R = l;
}

C_BLOWFISH *bfcodec_init(void) {
    C_BLOWFISH *blf = (C_BLOWFISH *)malloc(sizeof(C_BLOWFISH));
    if (!blf) {
        return NULL;
    }

    size_t idx = 0;
    unsigned int i, j, k;

    for (i = 0; i < 18; i++) {
        blf->p[i] = read_u32_be(&BF_INIT_BYTES[idx]);
        idx += 4;
    }

    for (k = 0; k < 4; k++) {
        for (j = 0; j < 256; j++) {
            blf->s[k][j] = read_u32_be(&BF_INIT_BYTES[idx]);
            idx += 4;
        }
    }

    return blf;
}

void bfcodec_expand_key(C_BLOWFISH *blf, const uint8_t *key, size_t key_len) {
    size_t j = 0;
    unsigned int i;

    if (key_len == 0) {
        return;
    }

    for (i = 0; i < 18; i++) {
        uint32_t k = (uint32_t)key[j % key_len] << 24 | (uint32_t)key[(j + 1) % key_len] << 16 |
                     (uint32_t)key[(j + 2) % key_len] << 8 | (uint32_t)key[(j + 3) % key_len];
        blf->p[i] ^= k;
        j += 4;
    }

    uint32_t L = 0, R = 0;
    for (i = 0; i < 18; i += 2) {
        bf_encrypt_block(blf, &L, &R);
        blf->p[i] = L;
        blf->p[i + 1] = R;
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 256; j += 2) {
            bf_encrypt_block(blf, &L, &R);
            blf->s[i][j] = L;
            blf->s[i][j + 1] = R;
        }
    }
}

void bfcodec_decrypt(C_BLOWFISH *blf, uint8_t *data, size_t len, const uint8_t iv[8]) {
    uint8_t prev[8];
    memcpy(prev, iv, 8);

    for (size_t off = 0; len >= 8; off += 8, len -= 8) {
        uint8_t cipher[8];
        memcpy(cipher, &data[off], 8);
        uint32_t L = read_u32_be(&data[off]);
        uint32_t R = read_u32_be(&data[off + 4]);
        bf_decrypt_block(blf, &L, &R);
        write_u32_be(&data[off], L);
        write_u32_be(&data[off + 4], R);
        for (size_t i = 0; i < 8; i++) {
            data[off + i] ^= prev[i];
        }
        memcpy(prev, cipher, 8);
    }
}

void bfcodec_encrypt(C_BLOWFISH *blf, uint8_t *data, size_t len, const uint8_t iv[8]) {
    uint8_t prev[8];
    memcpy(prev, iv, 8);

    for (size_t off = 0; len >= 8; off += 8, len -= 8) {
        for (size_t i = 0; i < 8; i++) {
            data[off + i] ^= prev[i];
        }
        uint32_t L = read_u32_be(&data[off]);
        uint32_t R = read_u32_be(&data[off + 4]);
        bf_encrypt_block(blf, &L, &R);
        write_u32_be(&data[off], L);
        write_u32_be(&data[off + 4], R);
        memcpy(prev, &data[off], 8);
    }
}
