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

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <argparse/argparse.hpp>

#ifdef HAVE_COREFOUNDATION
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef HAVE_LIBPLIST
#include <plist/plist.h>
#endif

#include "BFCodec.hpp"

namespace tools {

namespace fs = std::filesystem;

struct KeyIv {
    std::vector<std::byte> keyBytes;
    std::array<std::byte, 8> ivBytes;
};

inline int fromHexChar(char c, std::string &error) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    error = "invalid hex character";
    return -1;
}

inline std::expected<std::vector<std::byte>, std::string> parseHex(std::string_view hex) {
    std::string err;
    std::vector<std::byte> out;
    int nibble = -1;

    for (char c : hex) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            continue;
        }
        int v = fromHexChar(c, err);
        if (v < 0) {
            return std::unexpected(err);
        }
        if (nibble < 0) {
            nibble = v;
        } else {
            out.push_back(static_cast<std::byte>((nibble << 4) | v));
            nibble = -1;
        }
    }

    if (nibble >= 0) {
        return std::unexpected("odd number of hex digits");
    }

    return out;
}

inline std::expected<std::vector<std::byte>, std::string> readFileExactly(const fs::path &path,
                                                                          size_t expectedSize) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return std::unexpected("cannot open " + path.string());
    }

    std::vector<std::byte> buf(expectedSize);
    f.read(reinterpret_cast<char *>(buf.data()), static_cast<std::streamsize>(expectedSize));
    if (!f || f.get() != std::char_traits<char>::eof()) {
        return std::unexpected(path.string() + ": file size must be exactly " +
                               std::to_string(expectedSize) + " bytes");
    }

    return buf;
}

inline std::expected<KeyIv, std::string> getKeyIv(argparse::ArgumentParser &program) {
    auto keyOpt = program.present<std::string>("--key");
    auto keyFileOpt = program.present<std::string>("--key-file");
    auto ivOpt = program.present<std::string>("--iv");
    auto ivFileOpt = program.present<std::string>("--iv-file");

    if (keyOpt && keyFileOpt) {
        return std::unexpected("use -K/--key or --key-file, not both");
    }
    if (!keyOpt && !keyFileOpt) {
        return std::unexpected("key is required (-K/--key or --key-file)");
    }
    if (ivOpt && ivFileOpt) {
        return std::unexpected("use --iv or --iv-file, not both");
    }
    if (!ivOpt && !ivFileOpt) {
        return std::unexpected("IV is required (--iv or --iv-file)");
    }

    KeyIv out;

    if (keyOpt) {
        auto parsed = parseHex(*keyOpt);
        if (!parsed) {
            return std::unexpected("key: " + parsed.error());
        }
        if (parsed->empty()) {
            return std::unexpected("key must not be empty");
        }
        out.keyBytes = std::move(*parsed);
    } else {
        auto read = readFileExactly(*keyFileOpt, 16);
        if (!read) {
            return std::unexpected(read.error());
        }
        out.keyBytes = std::move(*read);
    }

    std::vector<std::byte> ivVec;
    if (ivOpt) {
        auto parsed = parseHex(*ivOpt);
        if (!parsed) {
            return std::unexpected("IV: " + parsed.error());
        }
        if (parsed->size() != 8) {
            return std::unexpected("IV must be exactly 8 bytes (16 hex digits), got " +
                                   std::to_string(parsed->size()) + " bytes");
        }
        ivVec = std::move(*parsed);
    } else {
        auto read = readFileExactly(*ivFileOpt, 8);
        if (!read) {
            return std::unexpected(read.error());
        }
        ivVec = std::move(*read);
    }

    std::copy_n(ivVec.begin(), 8, out.ivBytes.begin());
    return out;
}

inline std::expected<BFCodec, std::string> createCodec(const std::vector<std::byte> &keyBytes) {
    auto codec = BFCodec::create();
    if (!codec) {
        return std::unexpected("init failed");
    }

    auto expand = codec->expandKey(keyBytes);
    if (!expand) {
        return std::unexpected("expand key failed");
    }

    return std::move(*codec);
}

inline std::expected<std::vector<uint8_t>, std::string> readWholeFile(const fs::path &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return std::unexpected("cannot open " + path.string());
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    return data;
}

inline std::expected<void, std::string> writeWholeFile(const fs::path &path,
                                                       std::span<const uint8_t> data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        return std::unexpected("cannot open " + path.string() + " for write");
    }
    f.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!f) {
        return std::unexpected("write failed for " + path.string());
    }
    return {};
}

inline bool isBinaryPlist(std::span<const uint8_t> data) {
    static constexpr std::array<uint8_t, 6> kMagic = {'b', 'p', 'l', 'i', 's', 't'};
    if (data.size() < kMagic.size()) {
        return false;
    }
    return std::equal(kMagic.begin(), kMagic.end(), data.begin());
}

#if defined(HAVE_COREFOUNDATION)
inline std::expected<std::vector<uint8_t>, std::string> bplistToXml(std::span<const uint8_t> data) {
    CFDataRef cfData = CFDataCreate(kCFAllocatorDefault,
                                    reinterpret_cast<const UInt8 *>(data.data()),
                                    static_cast<CFIndex>(data.size()));
    if (!cfData) {
        return std::unexpected("CFDataCreate failed");
    }

    CFErrorRef error = nullptr;
    CFPropertyListRef plist = CFPropertyListCreateWithData(
        kCFAllocatorDefault, cfData, kCFPropertyListImmutable, nullptr, &error);
    CFRelease(cfData);

    if (!plist) {
        if (error) {
            CFRelease(error);
        }
        return std::unexpected("CFPropertyListCreateWithData failed");
    }

    CFDataRef xmlData = CFPropertyListCreateData(
        kCFAllocatorDefault, plist, kCFPropertyListXMLFormat_v1_0, 0, &error);
    CFRelease(plist);

    if (!xmlData) {
        if (error) {
            CFRelease(error);
        }
        return std::unexpected("CFPropertyListCreateData failed");
    }

    const UInt8 *bytes = CFDataGetBytePtr(xmlData);
    CFIndex length = CFDataGetLength(xmlData);
    std::vector<uint8_t> out(bytes, bytes + length);
    CFRelease(xmlData);
    if (error) {
        CFRelease(error);
    }
    return out;
}
#elif defined(HAVE_LIBPLIST)
inline std::expected<std::vector<uint8_t>, std::string> bplistToXml(std::span<const uint8_t> data) {
    plist_t root = nullptr;
    plist_err_t err = plist_from_bin(
        reinterpret_cast<const char *>(data.data()), static_cast<uint32_t>(data.size()), &root);
    if (err != PLIST_ERR_SUCCESS || !root) {
        return std::unexpected("plist_from_bin failed");
    }

    char *xml = nullptr;
    uint32_t length = 0;
    err = plist_to_xml(root, &xml, &length);
    if (err != PLIST_ERR_SUCCESS || !xml) {
        plist_free(root);
        return std::unexpected("plist_to_xml failed");
    }

    std::vector<uint8_t> out(reinterpret_cast<uint8_t *>(xml),
                             reinterpret_cast<uint8_t *>(xml) + length);
    plist_mem_free(xml);
    plist_free(root);
    return out;
}
#endif

inline bool isPngData(std::span<const uint8_t> data) {
    static constexpr std::array<uint8_t, 8> kPngSig = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (data.size() < kPngSig.size()) {
        return false;
    }
    return std::equal(kPngSig.begin(), kPngSig.end(), data.begin());
}

inline bool containsCgBI(std::span<const uint8_t> data) {
    static constexpr std::array<uint8_t, 4> kCgBI = {'C', 'g', 'B', 'I'};
    if (data.size() < kCgBI.size()) {
        return false;
    }

    const size_t searchLimit = std::min<size_t>(data.size(), 1024 * 1024);
    auto begin = data.begin();
    auto end = data.begin() + static_cast<std::ptrdiff_t>(searchLimit);
    return std::search(begin, end, kCgBI.begin(), kCgBI.end()) != end;
}

inline int runProcess(const std::vector<std::string> &argv) {
    if (argv.empty()) {
        return -1;
    }

    std::vector<char *> args;
    args.reserve(argv.size() + 1);
    for (const auto &a : argv) {
        args.push_back(const_cast<char *>(a.c_str()));
    }
    args.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        execvp(args[0], args.data());
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

inline bool tryPngdefryInPlace(const fs::path &pngPath) {
    fs::path tmp = pngPath;
    tmp += ".pngfix";

#ifdef __APPLE__
    // Prefer CoreGraphics-compatible fix via pngcrush on macOS when available.
    int rc = runProcess({"xcrun",
                         "-sdk",
                         "iphoneos",
                         "pngcrush",
                         "-revert-iphone-optimizations",
                         pngPath.string(),
                         tmp.string()});
    if (rc == 0) {
        std::error_code ec;
        fs::rename(tmp, pngPath, ec);
        if (!ec) {
            return true;
        }
        fs::remove(tmp, ec);
    }
#endif

    int rc = runProcess({"pngdefry", "-o", tmp.string(), pngPath.string()});
    if (rc != 0) {
        rc = runProcess({"pngdefry", pngPath.string(), tmp.string()});
    }
    if (rc != 0) {
        std::error_code ec;
        fs::remove(tmp, ec);
        return false;
    }

    std::error_code ec;
    fs::rename(tmp, pngPath, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }

    return true;
}

inline fs::path safeJoinZipPath(const fs::path &root, const std::string &zipName) {
    fs::path rel = fs::path(zipName).lexically_normal();
    if (rel.is_absolute()) {
        return {};
    }
    for (const auto &part : rel) {
        if (part == "..") {
            return {};
        }
    }
    fs::path out = root / rel;
    return out;
}

} // namespace tools
