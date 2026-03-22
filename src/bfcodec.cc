#include <cstdlib>
#include <string>
#include <utility>

#include "bfcodecpp.h"

BFCodec::BFCodec(C_BLOWFISH *blf) noexcept
    : blf_(blf) { // LCOV_EXCL_LINE - empty ctor, only used by create().
} // LCOV_EXCL_LINE

std::string BFCodec::message(BFCodecError e) {
    switch (e) {
    case BFCodecError::InitFailed:
        return "Init failed.";
    case BFCodecError::InvalidCodec:
        return "Invalid codec.";
    case BFCodecError::DataSizeNotMultipleOf8:
        return "Data size not multiple of 8.";
    // LCOV_EXCL_START
    default:
        return "Unknown BFCodec error.";
        // LCOV_EXCL_STOP
    }
}

std::expected<BFCodec, BFCodecError> BFCodec::create() {
    C_BLOWFISH *p = bfcodec_init();
    // LCOV_EXCL_START - allocation failure path not exercised in tests.
    if (!p) {
        return std::unexpected(BFCodecError::InitFailed);
    }
    // LCOV_EXCL_STOP
    return BFCodec(p);
}

BFCodec::BFCodec(BFCodec &&other) noexcept : blf_(std::exchange(other.blf_, nullptr)) {
}

BFCodec &BFCodec::operator=(BFCodec &&other) noexcept {
    if (this != &other) {
        std::free(blf_);
        blf_ = std::exchange(other.blf_, nullptr);
    }
    return *this;
}

BFCodec::~BFCodec() {
    std::free(blf_);
}

std::expected<void, BFCodecError> BFCodec::expandKey(std::span<const std::byte> key) {
    if (!blf_) {
        return std::unexpected(BFCodecError::InvalidCodec);
    }
    if (!key.empty()) {
        bfcodec_expand_key(blf_, reinterpret_cast<const uint8_t *>(key.data()), key.size());
    }
    return {};
}

std::expected<void, BFCodecError> BFCodec::decrypt(std::span<std::byte> data,
                                                   std::span<const std::byte, 8> iv) {
    if (!blf_) {
        return std::unexpected(BFCodecError::InvalidCodec);
    }
    if (data.size() % 8 != 0) {
        return std::unexpected(BFCodecError::DataSizeNotMultipleOf8);
    }
    bfcodec_decrypt(blf_,
                    reinterpret_cast<uint8_t *>(data.data()),
                    data.size(),
                    reinterpret_cast<const uint8_t *>(iv.data()));
    return {};
}

std::expected<void, BFCodecError> BFCodec::encrypt(std::span<std::byte> data,
                                                   std::span<const std::byte, 8> iv) {
    if (!blf_) {
        return std::unexpected(BFCodecError::InvalidCodec);
    }
    if (data.size() % 8 != 0) {
        return std::unexpected(BFCodecError::DataSizeNotMultipleOf8);
    }
    bfcodec_encrypt(blf_,
                    reinterpret_cast<uint8_t *>(data.data()),
                    data.size(),
                    reinterpret_cast<const uint8_t *>(iv.data()));
    return {};
}
