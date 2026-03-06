#ifndef BFCODEC_H
#define BFCODEC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Blowfish codec state (custom P/S boxes, pi-derived).
 * All fields are private; use only via bfcodec_* functions.
 */
typedef struct C_BLOWFISH {
	uint32_t p[18];
	uint32_t s[4][256];
} C_BLOWFISH;

/**
 * Allocate and initialize a Blowfish codec with pi-derived P and S boxes.
 * Returns NULL on allocation failure.
 */
C_BLOWFISH *bfcodec_init(void);

/**
 * Expand key into the codec state (XOR P with key bytes, then expand via encrypt).
 * key_len may be any positive length; key bytes are used big-endian in 32-bit chunks.
 */
void bfcodec_expand_key(C_BLOWFISH *blf, const uint8_t *key, size_t key_len);

/**
 * Decrypt data in place using CBC with the given 8-byte IV.
 * len must be a multiple of 8.
 */
void bfcodec_decrypt(C_BLOWFISH *blf, uint8_t *data, size_t len, const uint8_t iv[8]);

/**
 * Encrypt data in place using CBC with the given 8-byte IV.
 * len must be a multiple of 8.
 */
void bfcodec_encrypt(C_BLOWFISH *blf, uint8_t *data, size_t len, const uint8_t iv[8]);

#ifdef __cplusplus
}
#endif

#endif /* BFCODEC_H */
