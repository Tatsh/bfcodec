#ifndef BFCODEC_HPP
#define BFCODEC_HPP

#include "bfcodec.h"
#include <cstddef>
#include <expected>
#include <span>

/**
 * C++23 wrapper around the bfcodec C library.
 * Use create() to obtain a codec; expandKey() and decrypt() return std::expected.
 */
enum class BFCodecError {
	InitFailed,
	InvalidCodec,
	DataSizeNotMultipleOf8,
};

class BFCodec {
public:
	BFCodec() = delete;

	/** Create codec (calls bfcodec_init()). */
	static std::expected<BFCodec, BFCodecError> create();

	BFCodec(const BFCodec&) = delete;
	BFCodec& operator=(const BFCodec&) = delete;

	BFCodec(BFCodec&& other) noexcept;
	BFCodec& operator=(BFCodec&& other) noexcept;

	~BFCodec();

	/** Expand key (calls bfcodec_expand_key). */
	std::expected<void, BFCodecError> expandKey(std::span<const std::byte> key);

	/** Decrypt data in place with CBC. data.size() must be a multiple of 8; iv must be 8 bytes. */
	std::expected<void, BFCodecError> decrypt(std::span<std::byte> data, std::span<const std::byte, 8> iv);

	/** Encrypt data in place with CBC. data.size() must be a multiple of 8; iv must be 8 bytes. */
	std::expected<void, BFCodecError> encrypt(std::span<std::byte> data, std::span<const std::byte, 8> iv);

private:
	explicit BFCodec(C_BLOWFISH* blf) noexcept;

	C_BLOWFISH* blf_;
};

#endif /* BFCODEC_HPP */
