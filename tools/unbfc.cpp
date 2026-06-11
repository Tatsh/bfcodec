#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>

#include "jbtcommon.h"

namespace fs = std::filesystem;

int main(int argc, char *argv[]) {
    std::span args(argv, static_cast<size_t>(argc));
    argparse::ArgumentParser program("unbfc", BFCODEC_VERSION);
    program.add_description("Decrypt a file in place with BFCodec, overwriting the input.");

    program.add_argument("-K", "--key").help("Passphrase; default: \"Konami Bemani Mobile iPad\".");
    program.add_argument("--key-file")
        .help("Path to key file (first 16 bytes used; overrides default passphrase).");
    program.add_argument("--uuid").help(
        "UUID key (dashes optional); the key is MD5 of its canonical uppercase form.");
    program.add_argument("--iv").help("IV as hex string (spaces ignored).");
    program.add_argument("--iv-file").help("Path to IV file (first 8 bytes used).");
    program.add_argument("--backup")
        .help("Back up the destination to <dest>.bak before overwriting it.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-o", "--output")
        .help("Write output to this path instead of overwriting the input.");

    program.add_argument("-V")
        .help("Print version and exit.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("file").help("Input file to decrypt.");

    spdlog::set_pattern("%v");

    for (size_t i = 1; i < args.size(); ++i) {
        if (std::string_view(args[i]) == "-V") {
            spdlog::info("unbfc v" BFCODEC_VERSION);
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
        spdlog::error("{}", BFCodec::message(codec.error()));
        return EXIT_FAILURE;
    }

    fs::path inPath(program.get<std::string>("file"));
    if (!fs::is_regular_file(inPath)) {
        spdlog::error("Not a file: {}.", inPath.string());
        return EXIT_FAILURE;
    }
    auto raw = Tools::readWholeFile(inPath);
    if (!raw) {
        spdlog::error("{}", Tools::message(raw.error()));
        return EXIT_FAILURE;
    }
    if (raw->size() < 8) {
        spdlog::error("File too small to contain a trailer: {}.", inPath.string());
        return EXIT_FAILURE;
    }

    const size_t totalSize = raw->size();
    const size_t trailerOffset = totalSize - 8u;
    std::span<const uint8_t> trailer = std::span(*raw).subspan(trailerOffset, 8u);
    const uint32_t fileSize =
        (static_cast<uint32_t>(trailer[0]) << 24) | (static_cast<uint32_t>(trailer[1]) << 16) |
        (static_cast<uint32_t>(trailer[2]) << 8) | static_cast<uint32_t>(trailer[3]);
    const uint32_t blockSize =
        (static_cast<uint32_t>(trailer[4]) << 24) | (static_cast<uint32_t>(trailer[5]) << 16) |
        (static_cast<uint32_t>(trailer[6]) << 8) | static_cast<uint32_t>(trailer[7]);

    const size_t available = trailerOffset;
    const size_t cipherLen = std::min<size_t>(blockSize, available);
    if (cipherLen == 0 || (cipherLen % 8u) != 0u) {
        spdlog::error("Invalid block size in trailer for {}.", inPath.string());
        return EXIT_FAILURE;
    }

    const size_t storedSize = std::min<size_t>(fileSize, cipherLen);
    std::vector<uint8_t> plain(cipherLen);
    std::copy_n(raw->begin(), cipherLen, plain.begin());

    std::span<std::byte> span = std::as_writable_bytes(std::span(plain).first(cipherLen));
    auto result = codec->decrypt(span, keyIv->ivBytes);
    if (!result) {
        spdlog::error("{} for {}.", BFCodec::message(result.error()), inPath.string());
        return EXIT_FAILURE;
    }

    plain.resize(storedSize);

    fs::path outPath = inPath;
    if (auto outputOpt = program.present<std::string>("--output")) {
        outPath = *outputOpt;
    }

    if (program.get<bool>("--backup") && fs::exists(outPath)) {
        auto backup = Tools::backupFile(outPath);
        if (!backup) {
            spdlog::error("{}", Tools::message(backup.error()));
            return EXIT_FAILURE;
        }
    }

    auto write =
        Tools::writeWholeFile(outPath, std::span<const uint8_t>(plain.data(), plain.size()));
    if (!write) {
        spdlog::error("{}", Tools::message(write.error()));
        return EXIT_FAILURE;
    }

    spdlog::info("Wrote {}.", outPath.string());
    return EXIT_SUCCESS;
}
