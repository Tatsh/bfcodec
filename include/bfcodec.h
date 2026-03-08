/** @file */
#ifndef BFCODEC_H
#define BFCODEC_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(BFCODEC_EXPORTS) || defined(bfcodec_EXPORTS)
#define BFCODEC_API __declspec(dllexport)
#elif defined(BFCODEC_USE_DLL)
#define BFCODEC_API __declspec(dllimport)
#else
#define BFCODEC_API
#endif
#else
#if defined(BFCODEC_EXPORTS) || defined(bfcodec_EXPORTS)
#define BFCODEC_API __attribute__((visibility("default")))
#else
#define BFCODEC_API
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Blowfish codec state. */
struct C_BLOWFISH;
typedef struct C_BLOWFISH C_BLOWFISH;

/**
 * @brief Allocate and initialize a Blowfish codec with pi-derived P and S boxes.
 * @return A new codec instance, or NULL on allocation failure.
 */
BFCODEC_API C_BLOWFISH *bfcodec_init(void);

/**
 * @brief Expand key into the codec state (XOR P with key bytes, then expand via encrypt).
 * @param blf Codec instance from bfcodec_init().
 * @param key Key bytes; key_len may be any positive length.
 * @param key_len Length of key in bytes; key bytes are used big-endian in 32-bit chunks.
 */
BFCODEC_API void bfcodec_expand_key(C_BLOWFISH *blf, const uint8_t *key, size_t key_len);

/**
 * @brief Decrypt data in place using CBC with the given 8-byte IV.
 * @param blf Codec instance with key already expanded.
 * @param data Buffer to decrypt in place.
 * @param len Length of data in bytes; must be a multiple of 8.
 * @param iv Initialisation vector; exactly 8 bytes.
 */
BFCODEC_API void bfcodec_decrypt(C_BLOWFISH *blf, uint8_t *data, size_t len, const uint8_t iv[8]);

/**
 * @brief Encrypt data in place using CBC with the given 8-byte IV.
 * @param blf Codec instance with key already expanded.
 * @param data Buffer to encrypt in place.
 * @param len Length of data in bytes; must be a multiple of 8.
 * @param iv Initialisation vector; exactly 8 bytes.
 */
BFCODEC_API void bfcodec_encrypt(C_BLOWFISH *blf, uint8_t *data, size_t len, const uint8_t iv[8]);

#ifdef __cplusplus
}
#endif

#endif /* BFCODEC_H */
