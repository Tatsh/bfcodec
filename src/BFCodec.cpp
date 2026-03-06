#include <cstdlib>
#include <utility>

#include "BFCodec.hpp"

BFCodec::BFCodec(C_BLOWFISH *blf) noexcept : blf_(blf) {
}

std::expected<BFCodec, BFCodecError> BFCodec::create() {
    C_BLOWFISH *p = bfcodec_init();
    if (!p) {
        return std::unexpected(BFCodecError::InitFailed);
    }
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
