#include <array>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "BFCodec.hpp"

namespace {

std::array<std::byte, 8> make_iv(uint8_t a = 0) {
    std::array<std::byte, 8> iv{};
    for (size_t i = 0; i < 8; ++i) {
        iv[i] = static_cast<std::byte>(static_cast<uint8_t>(a + i));
    }
    return iv;
}

} // namespace

TEST(BFCodec, CreateSucceeds) {
    auto result = BFCodec::create();
    ASSERT_TRUE(result.has_value()) << "create() should succeed";
    EXPECT_TRUE(result->expandKey(std::span<const std::byte>{}).has_value());
}

TEST(BFCodec, ExpandKeyEmptyKeySucceeds) {
    auto codec = BFCodec::create();
    ASSERT_TRUE(codec.has_value());
    std::vector<std::byte> empty;
    auto result = codec->expandKey(empty);
    EXPECT_TRUE(result.has_value());
}

TEST(BFCodec, ExpandKeyNonEmptySucceeds) {
    auto codec = BFCodec::create();
    ASSERT_TRUE(codec.has_value());
    std::array<std::byte, 4> key = {std::byte{'t'}, std::byte{'e'}, std::byte{'s'}, std::byte{'t'}};
    auto result = codec->expandKey(key);
    EXPECT_TRUE(result.has_value());
}

TEST(BFCodec, EncryptDecryptRoundTrip) {
    auto codec = BFCodec::create();
    ASSERT_TRUE(codec.has_value());

    std::vector<std::byte> key = {std::byte{0x01},
                                  std::byte{0x02},
                                  std::byte{0x03},
                                  std::byte{0x04},
                                  std::byte{0x05},
                                  std::byte{0x06},
                                  std::byte{0x07},
                                  std::byte{0x08}};
    auto exp = codec->expandKey(key);
    ASSERT_TRUE(exp.has_value());

    std::array<std::byte, 8> iv = make_iv(0x10);
    std::vector<std::byte> plain = {std::byte{'h'},
                                    std::byte{'e'},
                                    std::byte{'l'},
                                    std::byte{'l'},
                                    std::byte{'o'},
                                    std::byte{'!'},
                                    std::byte{0},
                                    std::byte{0}};

    std::vector<std::byte> cipher = plain;
    auto enc = codec->encrypt(cipher, iv);
    ASSERT_TRUE(enc.has_value());
    EXPECT_NE(cipher, plain);

    auto dec = codec->decrypt(cipher, iv);
    ASSERT_TRUE(dec.has_value());
    EXPECT_EQ(cipher, plain);
}

TEST(BFCodec, EncryptDecryptRoundTripMultipleBlocks) {
    auto codec = BFCodec::create();
    ASSERT_TRUE(codec.has_value());

    std::array<std::byte, 16> key = {};
    for (size_t i = 0; i < 16; ++i) {
        key[i] = static_cast<std::byte>(i);
    }
    auto exp = codec->expandKey(key);
    ASSERT_TRUE(exp.has_value());

    std::array<std::byte, 8> iv = make_iv(0x20);
    std::vector<std::byte> plain(16, std::byte{0});
    plain[0] = std::byte{'A'};
    plain[1] = std::byte{'B'};
    plain[8] = std::byte{'C'};
    plain[9] = std::byte{'D'};

    std::vector<std::byte> cipher = plain;
    auto enc = codec->encrypt(cipher, iv);
    ASSERT_TRUE(enc.has_value());
    EXPECT_NE(cipher, plain);

    auto dec = codec->decrypt(cipher, iv);
    ASSERT_TRUE(dec.has_value());
    EXPECT_EQ(cipher, plain);
}

TEST(BFCodec, DecryptDataSizeNotMultipleOf8Fails) {
    auto codec = BFCodec::create();
    ASSERT_TRUE(codec.has_value());
    codec->expandKey(
        std::array<std::byte, 4>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});

    std::vector<std::byte> data(7, std::byte{0});
    std::array<std::byte, 8> iv = make_iv();
    auto result = codec->decrypt(data, iv);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BFCodecError::DataSizeNotMultipleOf8);
}

TEST(BFCodec, EncryptDataSizeNotMultipleOf8Fails) {
    auto codec = BFCodec::create();
    ASSERT_TRUE(codec.has_value());
    codec->expandKey(
        std::array<std::byte, 4>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});

    std::vector<std::byte> data(10, std::byte{0});
    std::array<std::byte, 8> iv = make_iv();
    auto result = codec->encrypt(data, iv);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BFCodecError::DataSizeNotMultipleOf8);
}

TEST(BFCodec, MovedFromCodecExpandKeyFails) {
    auto codec = BFCodec::create();
    ASSERT_TRUE(codec.has_value());
    BFCodec other = std::move(*codec);

    std::array<std::byte, 4> key = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    auto result = codec->expandKey(key);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BFCodecError::InvalidCodec);
}

TEST(BFCodec, MovedFromCodecEncryptFails) {
    auto codec = BFCodec::create();
    ASSERT_TRUE(codec.has_value());
    codec->expandKey(
        std::array<std::byte, 4>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});
    BFCodec other = std::move(*codec);

    std::vector<std::byte> data(8, std::byte{0});
    std::array<std::byte, 8> iv = make_iv();
    auto result = codec->encrypt(data, iv);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BFCodecError::InvalidCodec);
}

TEST(BFCodec, MovedFromCodecDecryptFails) {
    auto codec = BFCodec::create();
    ASSERT_TRUE(codec.has_value());
    BFCodec other = std::move(*codec);

    std::vector<std::byte> data(8, std::byte{0});
    std::array<std::byte, 8> iv = make_iv();
    auto result = codec->decrypt(data, iv);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BFCodecError::InvalidCodec);
}

TEST(BFCodec, MoveConstructorTargetUsable) {
    auto codec = BFCodec::create();
    ASSERT_TRUE(codec.has_value());
    codec->expandKey(
        std::array<std::byte, 4>{std::byte{'k'}, std::byte{'e'}, std::byte{'y'}, std::byte{0}});

    BFCodec moved = std::move(*codec);
    std::vector<std::byte> data(8, std::byte{0});
    std::array<std::byte, 8> iv = make_iv();
    auto enc = moved.encrypt(data, iv);
    EXPECT_TRUE(enc.has_value());
}

TEST(BFCodec, MoveAssignmentTargetUsable) {
    auto a = BFCodec::create();
    auto b = BFCodec::create();
    ASSERT_TRUE(a.has_value() && b.has_value());
    a->expandKey(std::array<std::byte, 4>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});
    b->expandKey(std::array<std::byte, 4>{std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}});

    *a = std::move(*b);

    std::vector<std::byte> data(8, std::byte{0});
    std::array<std::byte, 8> iv = make_iv();
    auto enc = a->encrypt(data, iv);
    EXPECT_TRUE(enc.has_value());

    auto dec = b->decrypt(data, iv);
    ASSERT_FALSE(dec.has_value());
    EXPECT_EQ(dec.error(), BFCodecError::InvalidCodec);
}

TEST(BFCodec, DecryptEmptyDataSucceeds) {
    auto codec = BFCodec::create();
    ASSERT_TRUE(codec.has_value());
    codec->expandKey(
        std::array<std::byte, 4>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});

    std::vector<std::byte> data;
    std::array<std::byte, 8> iv = make_iv();
    auto result = codec->decrypt(data, iv);
    EXPECT_TRUE(result.has_value());
}

TEST(BFCodec, EncryptEmptyDataSucceeds) {
    auto codec = BFCodec::create();
    ASSERT_TRUE(codec.has_value());
    codec->expandKey(
        std::array<std::byte, 4>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});

    std::vector<std::byte> data;
    std::array<std::byte, 8> iv = make_iv();
    auto result = codec->encrypt(data, iv);
    EXPECT_TRUE(result.has_value());
}
