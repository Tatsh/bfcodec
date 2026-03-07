#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#include <stdlib.h> /* mkdtemp */
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <argparse/argparse.hpp>

#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#elif defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>

#include <wincrypt.h>
#else
#include <openssl/evp.h>
#include <openssl/md5.h>
#endif

#ifdef HAVE_COREFOUNDATION
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef HAVE_LIBPLIST
#include <plist/plist.h>
#endif

#include "bfcodecpp.h"

namespace Tools {

namespace fs = std::filesystem;

enum class ParseError { InvalidHexChar, OddHexDigits };
enum class FileError { CannotOpen, WriteFailed, WrongSize };
enum class KeyIvError {
    IvAndIvFileBoth,
    IvParseFailed,
    IvReadFailed,
    IvWrongSize,
    KeyAndKeyFileBoth,
    KeyEmpty,
    KeyMd5Failed,
    KeyReadFailed,
    KeyRequired,
};
enum class PlistError {
    CFDataCreateFailed,
    CFPropertyListCreateDataFailed,
    CFPropertyListCreateFailed,
    FromBinFailed,
    ToXmlFailed,
};

std::string message(ParseError e);
std::string message(FileError e);
std::string message(KeyIvError e);
std::string message(PlistError e);

inline constexpr std::array<std::byte, 8> kDefaultIv = {
    static_cast<std::byte>(0xE3),
    static_cast<std::byte>(0x66),
    static_cast<std::byte>(0x31),
    static_cast<std::byte>(0xDA),
    static_cast<std::byte>(0x2C),
    static_cast<std::byte>(0x85),
    static_cast<std::byte>(0xA0),
    static_cast<std::byte>(0x64),
};

struct KeyIv {
    std::vector<std::byte> keyBytes;
    std::array<std::byte, 8> ivBytes;
};

int fromHexChar(char c);

std::expected<std::vector<std::byte>, ParseError> parseHex(std::string_view hex);

std::expected<std::vector<std::byte>, FileError> readFileExactly(const fs::path &path,
                                                                 size_t expectedSize);

/** Read the first \a size bytes from \a path. File may be longer; only the first bytes are used. */
std::expected<std::vector<std::byte>, FileError> readFileFirst(const fs::path &path, size_t size);

std::expected<std::array<std::byte, 16>, KeyIvError> md5Key(std::span<const std::byte> data);

std::expected<KeyIv, KeyIvError> getKeyIv(argparse::ArgumentParser &program);

std::expected<BFCodec, BFCodecError> createCodec(const std::vector<std::byte> &keyBytes);

std::expected<std::vector<uint8_t>, FileError> readWholeFile(const fs::path &path);

std::expected<void, FileError> writeWholeFile(const fs::path &path, std::span<const uint8_t> data);

bool isBinaryPlist(std::span<const uint8_t> data);

#if defined(HAVE_COREFOUNDATION)
std::expected<std::vector<uint8_t>, PlistError> bplistToXml(std::span<const uint8_t> data);
#elif defined(HAVE_LIBPLIST)
std::expected<std::vector<uint8_t>, PlistError> bplistToXml(std::span<const uint8_t> data);
#endif

bool isPngData(std::span<const uint8_t> data);

bool containsCgbi(std::span<const uint8_t> data);

int runProcess(const std::vector<std::string> &argv);

bool tryPngdefryInPlace(const fs::path &pngPath);

fs::path safeJoinZipPath(const fs::path &root, const std::string &zipName);

} // namespace Tools
