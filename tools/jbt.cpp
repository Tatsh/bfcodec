#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>

#include <spdlog/spdlog.h>
#include <zip.h>

#include "jbtcommon.h"

namespace fs = std::filesystem;

int main(int argc, char *argv[]) {
    argparse::ArgumentParser program("jbt");
    program.add_description("Create a .jbt/.orb/.rb archive (zip) by encrypting all files in a "
                            "directory using BFCodec.");

    program.add_argument("-K", "--key").help("Passphrase; alternative to --key-file.");
    program.add_argument("--key-file")
        .help("Path to binary key file (first 16 bytes will be used).");
    program.add_argument("--iv").help("IV as hex string (spaces ignored).");
    program.add_argument("--iv-file").help("Path to binary IV file (exactly 8 bytes).");

    program.add_argument("-V")
        .help("Print version and exit.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("indir").help("Input directory.");
    program.add_argument("archive").help("Output archive file (.jbt/.orb/.rb).");

    spdlog::set_pattern("%v");

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-V") == 0) {
            spdlog::info("jbt v" BFCODEC_VERSION);
            return EXIT_SUCCESS;
        }
    }

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception &err) {
        spdlog::error("{}", err.what());
        std::ostringstream oss;
        oss << program;
        spdlog::error("{}", oss.str());
        return EXIT_FAILURE;
    }

    auto keyIv = Tools::getKeyIv(program);
    if (!keyIv) {
        spdlog::error("{}", Tools::message(keyIv.error()));
        return EXIT_FAILURE;
    }

    auto codec = Tools::createCodec(keyIv->keyBytes);
    if (!codec) {
        spdlog::error("{}", message(codec.error()));
        return EXIT_FAILURE;
    }

    fs::path inDir(program.get<std::string>("indir"));
    fs::path archivePath(program.get<std::string>("archive"));

    if (!fs::is_directory(inDir)) {
        spdlog::error("Not a directory: {}.", inDir.string());
        return EXIT_FAILURE;
    }

    int zipErr = 0;
    zip_t *zip = zip_open(archivePath.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    if (!zip) {
        zip_error_t err;
        zip_error_init_with_code(&err, zipErr);
        spdlog::error("{}", zip_error_strerror(&err));
        zip_error_fini(&err);
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

        auto plain = Tools::readWholeFile(filePath);
        if (!plain) {
            spdlog::error("{}", Tools::message(plain.error()));
            zip_close(zip);
            return EXIT_FAILURE;
        }

        const uint32_t fileSize = static_cast<uint32_t>(plain->size());
        std::vector<uint8_t> buf = std::move(*plain);
        const size_t paddedSize = (buf.size() + 7u) / 8u * 8u;
        buf.resize(paddedSize, 0);
        const uint32_t blockSize = static_cast<uint32_t>(buf.size());

        if (blockSize == 0 || (buf.size() % 8u) != 0u) {
            spdlog::error("Invalid block size for {}.", filePath.string());
            zip_close(zip);
            return EXIT_FAILURE;
        }

        std::span<std::byte> span = std::as_writable_bytes(std::span(buf));
        auto result = codec->encrypt(span, keyIv->ivBytes);
        if (!result) {
            spdlog::error("{} for {}.", message(result.error()), filePath.string());
            zip_close(zip);
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

        zip_uint8_t *copy = static_cast<zip_uint8_t *>(malloc(out.size()));
        if (!copy) {
            spdlog::error("Out of memory.");
            zip_close(zip);
            return EXIT_FAILURE;
        }
        std::memcpy(copy, out.data(), out.size());

        zip_source_t *src = zip_source_buffer(zip, copy, out.size(), 1);
        if (!src) {
            spdlog::error("{}", zip_strerror(zip));
            free(copy);
            zip_close(zip);
            return EXIT_FAILURE;
        }

        zip_int64_t index = zip_file_add(zip, zipName.c_str(), src, ZIP_FL_OVERWRITE);
        if (index < 0) {
            spdlog::error("{}", zip_strerror(zip));
            zip_source_free(src);
            zip_close(zip);
            return EXIT_FAILURE;
        }

        if (zip_set_file_compression(zip, static_cast<zip_uint64_t>(index), ZIP_CM_STORE, 0) < 0) {
            spdlog::error("{}", zip_strerror(zip));
            zip_close(zip);
            return EXIT_FAILURE;
        }

        ++encryptedCount;
    }

    if (zip_close(zip) < 0) {
        spdlog::error("Close failed.");
        return EXIT_FAILURE;
    }

    spdlog::info("Wrote {}, encrypted {} file(s).", archivePath.string(), encryptedCount);
    return EXIT_SUCCESS;
}
