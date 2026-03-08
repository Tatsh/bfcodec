
/** @file */
#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string>

#include "bfcodec.h"

/** Error codes for BFCodec operations. */
enum class BFCodecError {
    InitFailed,             /**< bfcodec_init() failed (e.g. allocation failure). */
    InvalidCodec,           /**< Codec instance is invalid (e.g. moved-from). */
    DataSizeNotMultipleOf8, /**< Data length is not a multiple of 8 bytes. */
};

/**
 * @brief C++23 wrapper around the bfcodec C library.
 *
 * Use create() to obtain a codec; expandKey(), decrypt(), and encrypt() return std::expected.
 */
class BFCODEC_API BFCodec {
public:
    BFCodec() = delete;

    /**
     * @brief Create a codec instance (calls bfcodec_init()).
     * @return The codec on success, or std::unexpected(BFCodecError::InitFailed) on failure.
     */
    static std::expected<BFCodec, BFCodecError> create();

    /**
     * @brief Human-readable message for BFCodecError.
     * @param e The error code.
     * @return A string message describing the error.
     */
    static std::string message(BFCodecError e);

    BFCodec(const BFCodec &) = delete;
    BFCodec &operator=(const BFCodec &) = delete;

    BFCodec(BFCodec &&other) noexcept;
    BFCodec &operator=(BFCodec &&other) noexcept;

    ~BFCodec();

    /**
     * @brief Expand key into the codec state (calls bfcodec_expand_key).
     * @param key Key bytes; may be any non-empty length.
     * @return void on success, or std::unexpected(BFCodecError) on failure.
     */
    std::expected<void, BFCodecError> expandKey(std::span<const std::byte> key);

    /**
     * @brief Decrypt data in place with CBC.
     * @param data Buffer to decrypt; data.size() must be a multiple of 8.
     * @param iv Initialisation vector; must be exactly 8 bytes.
     * @return void on success, or std::unexpected(BFCodecError) on failure.
     */
    std::expected<void, BFCodecError> decrypt(std::span<std::byte> data,
                                              std::span<const std::byte, 8> iv);

    /**
     * @brief Encrypt data in place with CBC.
     * @param data Buffer to encrypt; data.size() must be a multiple of 8.
     * @param iv Initialisation vector; must be exactly 8 bytes.
     * @return void on success, or std::unexpected(BFCodecError) on failure.
     */
    std::expected<void, BFCodecError> encrypt(std::span<std::byte> data,
                                              std::span<const std::byte, 8> iv);

private:
    explicit BFCodec(C_BLOWFISH *blf) noexcept;

    C_BLOWFISH *blf_;
};
