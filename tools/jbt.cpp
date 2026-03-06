#include <cstdlib>
#include <cstring>
#include <filesystem>
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

bool addFileToZip(zip_t *za,
                  const std::string &name,
                  std::vector<uint8_t> &&data,
                  std::string &error) {
    if (!za) {
        error = "zip handle is null";
        return false;
    }

    void *buf = std::malloc(data.size());
    if (!buf && !data.empty()) {
        error = "out of memory";
        return false;
    }
    if (!data.empty()) {
        std::memcpy(buf, data.data(), data.size());
    }

    zip_source_t *src = zip_source_buffer(za, buf, data.size(), 1);
    if (!src) {
        std::free(buf);
        error = "zip: cannot create source for " + name;
        return false;
    }

    zip_int64_t idx = zip_file_add(za, name.c_str(), src, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
    if (idx < 0) {
        zip_source_free(src);
        error = "zip: cannot add file " + name;
        return false;
    }

    return true;
}

} // namespace

int main(int argc, char *argv[]) {
    argparse::ArgumentParser program("jbt");
    program.add_description("Create a .jbt/.rb/.orb archive (zip) by encrypting all files in a "
                            "directory using BFCodec.");

    program.add_argument("-K", "--key")
        .help("key as hex string (spaces ignored); alternative to --key-file");
    program.add_argument("--key-file").help("path to binary key file (exactly 16 bytes)");
    program.add_argument("--iv").help(
        "IV as hex string (spaces ignored); alternative to --iv-file");
    program.add_argument("--iv-file").help("path to binary IV file (exactly 8 bytes)");

    program.add_argument("indir").help("input directory");
    program.add_argument("archive").help("output archive file (.jbt/.rb/.orb)");

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception &err) {
        std::cerr << "jbt: " << err.what() << "\n";
        std::cerr << program;
        return EXIT_FAILURE;
    }

    auto keyIv = tools::getKeyIv(program);
    if (!keyIv) {
        std::cerr << "jbt: " << keyIv.error() << "\n";
        return EXIT_FAILURE;
    }

    auto codec = tools::createCodec(keyIv->keyBytes);
    if (!codec) {
        std::cerr << "jbt: " << codec.error() << "\n";
        return EXIT_FAILURE;
    }

    fs::path inDir(program.get<std::string>("indir"));
    fs::path archivePath(program.get<std::string>("archive"));

    if (!fs::is_directory(inDir)) {
        std::cerr << "jbt: not a directory: " << inDir.string() << "\n";
        return EXIT_FAILURE;
    }

    int err = 0;
    zip_t *za = zip_open(archivePath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!za) {
        std::cerr << "jbt: zip open failed: " << zipErrorToString(err) << "\n";
        return EXIT_FAILURE;
    }

    size_t encryptedCount = 0;

    for (auto it = fs::recursive_directory_iterator(inDir);
         it != fs::recursive_directory_iterator();
         ++it) {
        if (!it->is_regular_file()) {
            continue;
        }

        fs::path filePath = it->path();
        fs::path rel = fs::relative(filePath, inDir);
        std::string zipName = rel.generic_string();

        auto plain = tools::readWholeFile(filePath);
        if (!plain) {
            std::cerr << "jbt: " << plain.error() << "\n";
            zip_close(za);
            return EXIT_FAILURE;
        }

        const uint32_t fileSize = static_cast<uint32_t>(plain->size());
        std::vector<uint8_t> buf = std::move(*plain);
        const size_t paddedSize = (buf.size() + 7u) / 8u * 8u;
        buf.resize(paddedSize, 0);
        const uint32_t blockSize = static_cast<uint32_t>(buf.size());

        if (blockSize == 0 || (buf.size() % 8u) != 0u) {
            std::cerr << "jbt: invalid block size for " << filePath.string() << "\n";
            zip_close(za);
            return EXIT_FAILURE;
        }

        std::span<std::byte> span(reinterpret_cast<std::byte *>(buf.data()), buf.size());
        auto result = codec->encrypt(span, keyIv->ivBytes);
        if (!result) {
            std::cerr << "jbt: encrypt failed for " << filePath.string() << "\n";
            zip_close(za);
            return EXIT_FAILURE;
        }

        std::vector<uint8_t> out;
        out.reserve(buf.size() + 8u);
        out.insert(out.end(), buf.begin(), buf.end());
        out.push_back(static_cast<uint8_t>((fileSize >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((fileSize >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((fileSize >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(fileSize & 0xFF));
        out.push_back(static_cast<uint8_t>((blockSize >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((blockSize >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((blockSize >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(blockSize & 0xFF));

        std::string addError;
        if (!addFileToZip(za, zipName, std::move(out), addError)) {
            std::cerr << "jbt: " << addError << "\n";
            zip_close(za);
            return EXIT_FAILURE;
        }

        ++encryptedCount;
    }

    if (zip_close(za) != 0) {
        std::cerr << "jbt: zip close failed\n";
        return EXIT_FAILURE;
    }

    std::cerr << "jbt: wrote " << archivePath.string() << ", encrypted " << encryptedCount
              << " file(s)\n";
    return EXIT_SUCCESS;
}
