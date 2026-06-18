#include <cstdlib>
#include <filesystem>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

#include "jbtarchive.h"
#include "jbtcommon.h"

namespace fs = std::filesystem;

int main(int argc, char *argv[]) {
    std::span args(argv, static_cast<size_t>(argc));
    argparse::ArgumentParser program("jbt");
    program.add_description("Create a .jbt/.orb/.rb archive (zip) by encrypting all files in a "
                            "directory using BFCodec.");

    program.add_argument("-K", "--key").help("Passphrase; default: \"Konami Bemani Mobile iPad\".");
    program.add_argument("--key-file")
        .help("Path to key file (first 16 bytes used; overrides default passphrase).");
    program.add_argument("--iv").help("IV as hex string (spaces ignored).");
    program.add_argument("--iv-file").help("Path to IV file (first 8 bytes used).");

    program.add_argument("-V")
        .help("Print version and exit.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("indir").help("Input directory.");
    program.add_argument("archive").help("Output archive file (.jbt/.orb/.rb).");

    spdlog::set_pattern("%v");

    for (size_t i = 1; i < args.size(); ++i) {
        if (std::string_view(args[i]) == "-V") {
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
        spdlog::error("{}", BFCodec::message(codec.error()));
        return EXIT_FAILURE;
    }

    fs::path inDir(program.get<std::string>("indir"));
    fs::path archivePath(program.get<std::string>("archive"));

    if (!fs::is_directory(inDir)) {
        spdlog::error("Not a directory: {}.", inDir.string());
        return EXIT_FAILURE;
    }

    auto encryptedCount = Tools::packArchive(inDir, archivePath, *codec, keyIv->ivBytes);
    if (!encryptedCount) {
        return EXIT_FAILURE;
    }

    spdlog::info("Wrote {}, encrypted {} file(s).", archivePath.string(), *encryptedCount);
    return EXIT_SUCCESS;
}
