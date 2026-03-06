#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>

#include <argparse/argparse.hpp>

#include "bfcodecpp.h"

namespace {

enum class Mode { Decode, Encode };

int fromHexChar(char c, std::string &error) {
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

/** Parse hex string; spaces are ignored. Returns bytes or nullopt on error. */
std::expected<std::vector<std::byte>, std::string> parseHex(std::string_view hex) {
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

bool writeToStdout(bool outfileIsStdout, bool allowTextToTty) {
    if (!outfileIsStdout) {
        return false;
    }
    if (allowTextToTty) {
        return true;
    }
    return !isatty(STDOUT_FILENO);
}

/** Read exactly expectedSize bytes from a binary file. Fails if file size != expectedSize. */
std::expected<std::vector<std::byte>, std::string> readFileExactly(const std::string &path,
                                                                   size_t expectedSize) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return std::unexpected("cannot open " + path);
    }
    std::vector<std::byte> buf(expectedSize);
    f.read(reinterpret_cast<char *>(buf.data()), static_cast<std::streamsize>(expectedSize));
    if (!f || f.get() != std::char_traits<char>::eof()) {
        return std::unexpected(path + ": file size must be exactly " +
                               std::to_string(expectedSize) + " bytes");
    }
    return buf;
}

} // namespace

int main(int argc, char *argv[]) {
    argparse::ArgumentParser program("bfcodec");
    program.add_description(
        "Encode or decode files using the BFCodec (Blowfish-derived) cipher with CBC.");
    program.add_epilog(
        "-K/--key and --iv are hex strings (spaces ignored). IV must be 16 hex chars (8 bytes).\n"
        "--key-file and --iv-file are strictly binary (16 bytes and 8 bytes respectively).\n"
        "If OUTFILE is omitted or -, output goes to stdout (refused if stdout is a terminal unless "
        "--text).");

    program.add_argument("mode")
        .help("operation: dec (decrypt) or enc (encrypt)")
        .metavar("dec|enc");
    program.add_argument("-K", "--key")
        .help("key (hex string, spaces ignored); use -K or --key-file");
    program.add_argument("--key-file")
        .help("path to binary file containing key (exactly 16 bytes; alternative to -K)");
    program.add_argument("--iv").help(
        "IV (16 hex chars = 8 bytes, spaces ignored); use --iv or --iv-file");
    program.add_argument("--iv-file")
        .help("path to binary file containing IV (exactly 8 bytes; alternative to --iv)");
    program.add_argument("--text").flag().help("allow binary output to terminal (stdout)");
    program.add_argument("-S", "--output-size")
        .help("(dec only) trim decrypted output to this many bytes if larger")
        .scan<'i', int>();
    program.add_argument("infile").help("input file");
    program.add_argument("outfile")
        .nargs(argparse::nargs_pattern::optional)
        .help("output file; - or omit for stdout");

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception &err) {
        std::cerr << "bfcodec: " << err.what() << "\n";
        std::cerr << program;
        return EXIT_FAILURE;
    }

    std::string modeStr = program.get<std::string>("mode");
    Mode mode;
    if (modeStr == "dec") {
        mode = Mode::Decode;
    } else if (modeStr == "enc") {
        mode = Mode::Encode;
    } else {
        std::cerr << "bfcodec: mode must be dec or enc\n";
        std::cerr << program;
        return EXIT_FAILURE;
    }

    auto keyOpt = program.present<std::string>("--key");
    auto keyFileOpt = program.present<std::string>("--key-file");
    auto ivOpt = program.present<std::string>("--iv");
    auto ivFileOpt = program.present<std::string>("--iv-file");

    if (keyOpt && keyFileOpt) {
        std::cerr << "bfcodec: use -K/--key or --key-file, not both\n";
        return EXIT_FAILURE;
    }
    if (!keyOpt && !keyFileOpt) {
        std::cerr << "bfcodec: key is required (-K/--key or --key-file)\n";
        return EXIT_FAILURE;
    }
    if (ivOpt && ivFileOpt) {
        std::cerr << "bfcodec: use --iv or --iv-file, not both\n";
        return EXIT_FAILURE;
    }
    if (!ivOpt && !ivFileOpt) {
        std::cerr << "bfcodec: IV is required (--iv or --iv-file)\n";
        return EXIT_FAILURE;
    }

    std::vector<std::byte> keyBytes;
    if (keyOpt) {
        auto parsed = parseHex(*keyOpt);
        if (!parsed) {
            std::cerr << "bfcodec: key: " << parsed.error() << "\n";
            return EXIT_FAILURE;
        }
        if (parsed->empty()) {
            std::cerr << "bfcodec: key must not be empty\n";
            return EXIT_FAILURE;
        }
        keyBytes = std::move(*parsed);
    } else {
        auto read = readFileExactly(*keyFileOpt, 16);
        if (!read) {
            std::cerr << "bfcodec: " << read.error() << "\n";
            return EXIT_FAILURE;
        }
        keyBytes = std::move(*read);
    }

    std::vector<std::byte> ivBytes;
    if (ivOpt) {
        auto parsed = parseHex(*ivOpt);
        if (!parsed) {
            std::cerr << "bfcodec: IV: " << parsed.error() << "\n";
            return EXIT_FAILURE;
        }
        if (parsed->size() != 8) {
            std::cerr << "bfcodec: IV must be exactly 8 bytes (16 hex digits), got "
                      << parsed->size() << " bytes\n";
            return EXIT_FAILURE;
        }
        ivBytes = std::move(*parsed);
    } else {
        auto read = readFileExactly(*ivFileOpt, 8);
        if (!read) {
            std::cerr << "bfcodec: " << read.error() << "\n";
            return EXIT_FAILURE;
        }
        ivBytes = std::move(*read);
    }

    bool allowTextToTty = program.get<bool>("--text");
    std::optional<int> outputSizeOpt = program.present<int>("--output-size");
    std::string infile = program.get<std::string>("infile");

    std::optional<std::string> outfile;
    if (auto out = program.present<std::string>("outfile")) {
        if (*out != "-") {
            outfile = *out;
        }
    }
    bool outfileIsStdout = !outfile.has_value();

    if (outfileIsStdout && !writeToStdout(outfileIsStdout, allowTextToTty)) {
        std::cerr << "bfcodec: stdout is a terminal; use --text to allow binary output, or specify "
                     "OUTFILE\n";
        return EXIT_FAILURE;
    }

    if (outputSizeOpt && *outputSizeOpt < 0) {
        std::cerr << "bfcodec: --output-size must be non-negative\n";
        return EXIT_FAILURE;
    }

    auto codec = BFCodec::create();
    if (!codec) {
        std::cerr << "bfcodec: init failed\n";
        return EXIT_FAILURE;
    }
    auto expand = codec->expandKey(keyBytes);
    if (!expand) {
        std::cerr << "bfcodec: expand key failed\n";
        return EXIT_FAILURE;
    }

    std::ifstream in(infile, std::ios::binary);
    if (!in) {
        std::cerr << "bfcodec: cannot open input file: " << infile << "\n";
        return EXIT_FAILURE;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    in.close();

    if (data.size() % 8 != 0) {
        std::cerr << "bfcodec: input size must be a multiple of 8 bytes\n";
        return EXIT_FAILURE;
    }

    std::array<std::byte, 8> iv;
    std::copy_n(ivBytes.begin(), 8, iv.begin());

    std::span<std::byte> dataSpan = std::as_writable_bytes(std::span(data));
    if (mode == Mode::Decode) {
        auto result = codec->decrypt(dataSpan, iv);
        if (!result) {
            std::cerr << "bfcodec: decrypt failed\n";
            return EXIT_FAILURE;
        }
        if (outputSizeOpt && data.size() > static_cast<size_t>(*outputSizeOpt)) {
            data.resize(static_cast<size_t>(*outputSizeOpt));
        }
    } else {
        auto result = codec->encrypt(dataSpan, iv);
        if (!result) {
            std::cerr << "bfcodec: encrypt failed\n";
            return EXIT_FAILURE;
        }
    }

    std::ostream *out = &std::cout;
    std::ofstream outFile;
    if (outfile.has_value()) {
        outFile.open(*outfile, std::ios::binary);
        if (!outFile) {
            std::cerr << "bfcodec: cannot open output file: " << *outfile << "\n";
            return EXIT_FAILURE;
        }
        out = &outFile;
    }
    out->write(reinterpret_cast<const char *>(data.data()),
               static_cast<std::streamsize>(data.size()));
    if (!*out) {
        std::cerr << "bfcodec: write failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
