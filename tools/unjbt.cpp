#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <zip.h>

#include "jbt_common.hpp"

namespace fs = std::filesystem;

namespace {

std::string zipErrorToString(int err) {
    zip_error_t ze;
    zip_error_init_with_code(&ze, err);
    std::string s = zip_error_strerror(&ze);
    zip_error_fini(&ze);
    return s;
}

bool extractZip(const fs::path &archivePath, const fs::path &outDir, std::string &error) {
    int err = 0;
    zip_t *za = zip_open(archivePath.c_str(), ZIP_RDONLY, &err);
    if (!za) {
        error = "zip open failed: " + zipErrorToString(err);
        return false;
    }

    zip_int64_t count = zip_get_num_entries(za, 0);
    if (count < 0) {
        error = "zip: cannot get entry count";
        zip_close(za);
        return false;
    }

    for (zip_uint64_t i = 0; i < static_cast<zip_uint64_t>(count); ++i) {
        const char *name = zip_get_name(za, i, 0);
        if (!name) {
            error = "zip: cannot get entry name";
            zip_close(za);
            return false;
        }

        std::string entryName(name);
        fs::path outPath = tools::safeJoinZipPath(outDir, entryName);
        if (outPath.empty()) {
            error = "zip: unsafe path in archive: " + entryName;
            zip_close(za);
            return false;
        }

        if (!entryName.empty() && entryName.back() == '/') {
            std::error_code ec;
            fs::create_directories(outPath, ec);
            if (ec) {
                error = "cannot create directory: " + outPath.string();
                zip_close(za);
                return false;
            }
            continue;
        }

        std::error_code ec;
        fs::create_directories(outPath.parent_path(), ec);
        if (ec) {
            error = "cannot create directory: " + outPath.parent_path().string();
            zip_close(za);
            return false;
        }

        zip_file_t *zf = zip_fopen_index(za, i, 0);
        if (!zf) {
            error = "zip: cannot open entry: " + entryName;
            zip_close(za);
            return false;
        }

        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            zip_fclose(zf);
            error = "cannot open output file: " + outPath.string();
            zip_close(za);
            return false;
        }

        std::array<char, 64 * 1024> buf;
        while (true) {
            zip_int64_t n = zip_fread(zf, buf.data(), buf.size());
            if (n < 0) {
                zip_fclose(zf);
                error = "zip: read error for entry: " + entryName;
                zip_close(za);
                return false;
            }
            if (n == 0) {
                break;
            }
            out.write(buf.data(), static_cast<std::streamsize>(n));
            if (!out) {
                zip_fclose(zf);
                error = "write failed for: " + outPath.string();
                zip_close(za);
                return false;
            }
        }

        zip_fclose(zf);
    }

    if (zip_close(za) != 0) {
        error = "zip close failed";
        return false;
    }

    return true;
}

} // namespace

int main(int argc, char *argv[]) {
    argparse::ArgumentParser program("unjbt");
    program.add_description("Extract a .jbt/.rb/.orb archive (zip) and decrypt all extracted files "
                            "in place using BFCodec.");

    program.add_argument("-K", "--key")
        .help("key as hex string (spaces ignored); alternative to --key-file");
    program.add_argument("--key-file").help("path to binary key file (exactly 16 bytes)");
    program.add_argument("--iv").help(
        "IV as hex string (spaces ignored); alternative to --iv-file");
    program.add_argument("--iv-file").help("path to binary IV file (exactly 8 bytes)");

    program.add_argument("archive").help("input .jbt/.rb/.orb file (zip)");
    program.add_argument("outdir").help("output directory for extracted files");

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception &err) {
        std::cerr << "unjbt: " << err.what() << "\n";
        std::cerr << program;
        return EXIT_FAILURE;
    }

    auto keyIv = tools::getKeyIv(program);
    if (!keyIv) {
        std::cerr << "unjbt: " << keyIv.error() << "\n";
        return EXIT_FAILURE;
    }

    auto codec = tools::createCodec(keyIv->keyBytes);
    if (!codec) {
        std::cerr << "unjbt: " << codec.error() << "\n";
        return EXIT_FAILURE;
    }

    fs::path archivePath(program.get<std::string>("archive"));
    fs::path outDir(program.get<std::string>("outdir"));

    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        std::cerr << "unjbt: cannot create output directory: " << outDir.string() << "\n";
        return EXIT_FAILURE;
    }

    std::string extractError;
    if (!extractZip(archivePath, outDir, extractError)) {
        std::cerr << "unjbt: " << extractError << "\n";
        return EXIT_FAILURE;
    }

    size_t decryptedCount = 0;
    size_t skippedCount = 0;
    size_t pngFixedCount = 0;
    size_t bplistConvertedCount = 0;

    for (auto it = fs::recursive_directory_iterator(outDir);
         it != fs::recursive_directory_iterator();
         ++it) {
        if (!it->is_regular_file()) {
            continue;
        }

        const fs::path path = it->path();
        auto raw = tools::readWholeFile(path);
        if (!raw) {
            std::cerr << "unjbt: " << raw.error() << "\n";
            return EXIT_FAILURE;
        }

        if (raw->size() < 8) {
            ++skippedCount;
            continue;
        }

        const size_t totalSize = raw->size();
        const size_t trailerOffset = totalSize - 8u;
        const uint8_t *trailer = raw->data() + trailerOffset;
        const uint32_t fileSize =
            (static_cast<uint32_t>(trailer[0]) << 24) | (static_cast<uint32_t>(trailer[1]) << 16) |
            (static_cast<uint32_t>(trailer[2]) << 8) | static_cast<uint32_t>(trailer[3]);
        const uint32_t blockSize =
            (static_cast<uint32_t>(trailer[4]) << 24) | (static_cast<uint32_t>(trailer[5]) << 16) |
            (static_cast<uint32_t>(trailer[6]) << 8) | static_cast<uint32_t>(trailer[7]);

        const size_t available = trailerOffset;
        const size_t cipherLen = std::min<size_t>(blockSize, available);
        if (cipherLen == 0 || (cipherLen % 8u) != 0u) {
            ++skippedCount;
            continue;
        }

        const size_t storedSize = std::min<size_t>(fileSize, cipherLen);

        std::vector<uint8_t> plain(cipherLen);
        std::memcpy(plain.data(), raw->data(), cipherLen);

        std::span<std::byte> span(reinterpret_cast<std::byte *>(plain.data()), cipherLen);
        auto result = codec->decrypt(span, keyIv->ivBytes);
        if (!result) {
            std::cerr << "unjbt: decrypt failed for " << path.string() << "\n";
            return EXIT_FAILURE;
        }

        plain.resize(storedSize);

        const bool isPng = tools::isPngData(std::span<const uint8_t>(plain.data(), plain.size()));
        bool needsPngdefry = false;

        if (isPng && tools::containsCgBI(std::span<const uint8_t>(plain.data(), plain.size()))) {
            needsPngdefry = true;
        }

#ifdef HAVE_LIBPLIST
        bool isBplist = tools::isBinaryPlist(std::span<const uint8_t>(plain.data(), plain.size()));
        if (isBplist) {
            auto xml = tools::bplistToXml(std::span<const uint8_t>(plain.data(), plain.size()));
            if (xml) {
                plain = std::move(*xml);
                ++bplistConvertedCount;
            }
        }
#endif

        auto write =
            tools::writeWholeFile(path, std::span<const uint8_t>(plain.data(), plain.size()));
        if (!write) {
            std::cerr << "unjbt: " << write.error() << "\n";
            return EXIT_FAILURE;
        }

        ++decryptedCount;

        if (needsPngdefry) {
            if (tools::tryPngdefryInPlace(path)) {
                ++pngFixedCount;
            }
        }
    }

    std::cerr << "unjbt: extracted to " << outDir.string() << ", decrypted " << decryptedCount
              << " file(s), skipped " << skippedCount << " file(s)";
    if (pngFixedCount > 0) {
        std::cerr << ", fixed " << pngFixedCount << " CgBI PNG(s)";
    }
#ifdef HAVE_LIBPLIST
    if (bplistConvertedCount > 0) {
        std::cerr << ", converted " << bplistConvertedCount << " bplist(s) to XML";
    }
#endif
    std::cerr << "\n";

    return EXIT_SUCCESS;
}
