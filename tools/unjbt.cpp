#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#include <fnmatch.h>
#endif

#include <spdlog/spdlog.h>
#include <zip.h>

#include "jbtcommon.h"

namespace fs = std::filesystem;

namespace {

enum class ExtractError {
    CreateDirFailed,
    FileWriteFailed,
    ZipOpenFailed,
    ZipReadEntryFailed,
    ZipUnsafePath,
};

inline std::string message(ExtractError e) {
    switch (e) {
    case ExtractError::ZipOpenFailed:
        return "Zip open failed.";
    case ExtractError::ZipUnsafePath:
        return "Unsafe path in archive.";
    case ExtractError::ZipReadEntryFailed:
        return "Zip read entry failed.";
    case ExtractError::CreateDirFailed:
        return "Cannot create directory.";
    case ExtractError::FileWriteFailed:
        return "Write failed.";
    default:
        return "Unknown extract error.";
    }
}

struct Options {
    bool listShort = false;       // -l
    bool listZipinfo = false;     // -Z
    bool test = false;            // -t
    bool extractStdout = false;   // -c
    bool pipeStdout = false;      // -p
    bool freshen = false;         // -f
    bool update = false;          // -u
    bool neverOverwrite = false;  // -n
    bool junkPaths = false;       // -j
    int quiet = 0;                // -q = 1, -qq = 2
    bool verbose = false;         // -v
    bool caseInsensitive = false; // -C
    bool toLowercase = false;     // -LL
    bool wildcardNoSlash = false; // -W
    fs::path exdir = ".";
    std::vector<std::string> includePatterns;
    std::vector<std::string> excludePatterns;
};

std::string toLower(std::string s) {
    for (char &c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

#if defined(_WIN32)
static bool matchGlob(std::string_view path, std::string_view pattern, bool pathname) {
    if (pattern.empty()) {
        return path.empty();
    }
    if (pattern[0] == '*') {
        pattern.remove_prefix(1);
        for (size_t n = 0; n <= path.size(); ++n) {
            if (pathname && n > 0 && path[n - 1] == '/') {
                break;
            }
            if (matchGlob(path.substr(n), pattern, pathname)) {
                return true;
            }
        }
        return false;
    }
    if (pattern[0] == '?') {
        if (path.empty() || (pathname && path[0] == '/')) {
            return false;
        }
        return matchGlob(path.substr(1), pattern.substr(1), pathname);
    }
    if (path.empty() || path[0] != pattern[0]) {
        return false;
    }
    return matchGlob(path.substr(1), pattern.substr(1), pathname);
}
#endif

bool matchPattern(const std::string &path, const std::string &pattern, const Options &opts) {
    std::string p = path;
    std::string pat = pattern;
    if (opts.caseInsensitive) {
        p = toLower(p);
        pat = toLower(pat);
    }
#if defined(_WIN32)
    return matchGlob(p, pat, opts.wildcardNoSlash);
#else
    int flags = 0;
    if (opts.caseInsensitive) {
        flags |= FNM_CASEFOLD;
    }
    if (opts.wildcardNoSlash) {
        flags |= FNM_PATHNAME;
    }
    return fnmatch(pat.c_str(), p.c_str(), flags) == 0;
#endif
}

bool shouldInclude(const std::string &entryPath, const Options &opts) {
    for (const auto &x : opts.excludePatterns) {
        if (matchPattern(entryPath, x, opts))
            return false;
    }
    if (opts.includePatterns.empty())
        return true;
    for (const auto &i : opts.includePatterns) {
        if (matchPattern(entryPath, i, opts))
            return true;
    }
    return false;
}

std::string formatTime(time_t t) {
    if (t == 0) {
        return "00-00-1980 00:00";
    }
    struct tm tmBuf;
#ifdef _WIN32
    if (localtime_s(&tmBuf, &t) != 0) {
        return "00-00-1980 00:00";
    }
#else
    if (!localtime_r(&t, &tmBuf)) {
        return "00-00-1980 00:00";
    }
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%m-%d-%Y %H:%M", &tmBuf);
    return buf;
}

static bool isDirEntry(const char *name) {
    if (!name) {
        return false;
    }
    std::string_view sv(name);
    return !sv.empty() && sv.back() == '/';
}

void listShort(zip_t *zip, const Options &opts, const fs::path &archivePath) {
    std::ostringstream oss;
    if (opts.quiet < 2) {
        oss << "Archive:  " << archivePath.string() << "\n"
            << "  Length      Date    Time    Name\n"
            << "---------  ---------- -----   ----\n";
    }
    auto n = zip_get_num_entries(zip, 0);
    zip_uint64_t totalLen = 0;
    int count = 0;
    for (zip_int64_t i = 0; i < n; ++i) {
        const char *name = zip_get_name(zip, static_cast<zip_uint64_t>(i), 0);
        if (!name || !shouldInclude(name, opts)) {
            continue;
        }
        if (isDirEntry(name)) {
            continue;
        }
        zip_stat_t st;
        zip_stat_init(&st);
        if (zip_stat_index(zip, static_cast<zip_uint64_t>(i), 0, &st) < 0) {
            continue;
        }
        totalLen += st.size;
        count++;
        if (opts.quiet < 2) {
            oss << std::setw(9) << st.size << "  " << formatTime(st.mtime) << "   " << name << "\n";
        }
    }
    if (opts.quiet < 2) {
        oss << "---------                     -------\n"
            << std::setw(9) << totalLen << "                     " << count << " file(s)\n";
        spdlog::info("{}", oss.str());
    }
}

void listZipinfo(zip_t *zip, const Options &opts, const fs::path &archivePath) {
    std::ostringstream oss;
    if (opts.quiet < 2) {
        oss << "Archive:  " << archivePath.string() << "\n";
    }
    auto n = zip_get_num_entries(zip, 0);
    for (zip_int64_t i = 0; i < n; ++i) {
        const char *name = zip_get_name(zip, static_cast<zip_uint64_t>(i), 0);
        if (!name || !shouldInclude(name, opts)) {
            continue;
        }
        zip_stat_t st;
        zip_stat_init(&st);
        if (zip_stat_index(zip, static_cast<zip_uint64_t>(i), 0, &st) < 0) {
            continue;
        }
        if (opts.quiet < 2) {
            bool dir = isDirEntry(name);
            oss << "  " << (dir ? "drwxr-xr-x" : "-rw-r--r--") << "  " << std::setw(6) << st.size
                << "  " << formatTime(st.mtime) << "  " << name << "\n";
        }
    }
    if (opts.quiet < 2) {
        spdlog::info("{}", oss.str());
    }
}

std::expected<void, ExtractError> extractZip(const fs::path &archivePath,
                                             const fs::path &outDir,
                                             const Options &opts,
                                             std::function<bool(const std::string &)> filter) {
    int zipErr = 0;
    zip_t *zip = zip_open(archivePath.string().c_str(), ZIP_RDONLY, &zipErr);
    if (!zip) {
        return std::unexpected(ExtractError::ZipOpenFailed);
    }

    zip_int64_t n = zip_get_num_entries(zip, 0);
    for (zip_int64_t i = 0; i < n; ++i) {
        const char *name = zip_get_name(zip, static_cast<zip_uint64_t>(i), 0);
        if (!name || !filter(name)) {
            continue;
        }

        std::string entryName = name;
        if (opts.toLowercase) {
            for (char &c : entryName) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
        }

        fs::path outPath;
        if (opts.junkPaths) {
            fs::path p(entryName);
            outPath = outDir / p.filename();
        } else {
            outPath = Tools::safeJoinZipPath(outDir, entryName);
        }
        if (outPath.empty()) {
            zip_close(zip);
            return std::unexpected(ExtractError::ZipUnsafePath);
        }

        if (isDirEntry(name)) {
            std::error_code ec;
            fs::create_directories(outPath, ec);
            if (ec) {
                zip_close(zip);
                return std::unexpected(ExtractError::CreateDirFailed);
            }
            continue;
        }

        if (opts.neverOverwrite && fs::exists(outPath)) {
            continue;
        }

        zip_stat_t st;
        zip_stat_init(&st);
        if (zip_stat_index(zip, static_cast<zip_uint64_t>(i), 0, &st) < 0) {
            zip_close(zip);
            return std::unexpected(ExtractError::ZipReadEntryFailed);
        }
        if (opts.freshen || opts.update) {
            if (fs::exists(outPath)) {
                auto diskTime = fs::last_write_time(outPath);
                time_t entryTime = st.mtime;
                if (entryTime <= 0) {
                    entryTime = std::time(nullptr);
                }
                auto entrySys = std::chrono::system_clock::from_time_t(entryTime);
                auto entryFile = std::chrono::file_clock::from_sys(entrySys);
                if (diskTime >= entryFile && (opts.freshen || opts.update)) {
                    continue;
                }
            }
        }

        std::error_code ec;
        fs::create_directories(outPath.parent_path(), ec);
        if (ec) {
            zip_close(zip);
            return std::unexpected(ExtractError::CreateDirFailed);
        }

        zip_file_t *zf = zip_fopen_index(zip, static_cast<zip_uint64_t>(i), 0);
        if (!zf) {
            zip_close(zip);
            return std::unexpected(ExtractError::ZipReadEntryFailed);
        }
        std::vector<uint8_t> buf(st.size);
        zip_int64_t total = 0;
        while (static_cast<zip_uint64_t>(total) < st.size) {
            const zip_uint64_t remaining = st.size - static_cast<zip_uint64_t>(total);
            std::span<uint8_t> writeRegion(buf.data() + total, remaining);
            const zip_int64_t nr =
                zip_fread(zf, writeRegion.data(), static_cast<zip_uint64_t>(writeRegion.size()));
            if (nr <= 0) {
                zip_fclose(zf);
                zip_close(zip);
                return std::unexpected(ExtractError::ZipReadEntryFailed);
            }
            total += nr;
        }
        zip_fclose(zf);

        auto write =
            Tools::writeWholeFile(outPath, std::span<const uint8_t>(buf.data(), buf.size()));
        if (!write) {
            zip_close(zip);
            return std::unexpected(ExtractError::FileWriteFailed);
        }
    }

    zip_close(zip);
    return {};
}

void decryptExtractedFiles(const fs::path &outDir,
                           const Tools::KeyIv &keyIv,
                           BFCodec &codec,
                           size_t &decryptedCount,
                           size_t &skippedCount,
                           size_t &pngFixedCount,
                           size_t *bplistConvertedCount,
                           [[maybe_unused]] int quiet) {
    decryptedCount = 0;
    skippedCount = 0;
    pngFixedCount = 0;
    if (bplistConvertedCount) {
        *bplistConvertedCount = 0;
    }

    for (auto it = fs::recursive_directory_iterator(outDir);
         it != fs::recursive_directory_iterator();
         ++it) {
        if (!it->is_regular_file()) {
            continue;
        }

        const fs::path path = it->path();
        auto raw = Tools::readWholeFile(path);
        if (!raw) {
            continue;
        }

        if (raw->size() < 8) {
            ++skippedCount;
            continue;
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
            ++skippedCount;
            continue;
        }

        const size_t storedSize = std::min<size_t>(fileSize, cipherLen);
        std::vector<uint8_t> plain(cipherLen);
        std::copy_n(raw->begin(), cipherLen, plain.begin());

        std::span<std::byte> span = std::as_writable_bytes(std::span(plain).first(cipherLen));
        auto result = codec.decrypt(span, keyIv.ivBytes);
        if (!result) {
            continue;
        }

        plain.resize(storedSize);

        bool needsPngdefry = false;
        if (Tools::isPngData(std::span<const uint8_t>(plain.data(), plain.size())) &&
            Tools::containsCgbi(std::span<const uint8_t>(plain.data(), plain.size()))) {
            needsPngdefry = true;
        }

#ifdef HAVE_LIBPLIST
        if (Tools::isBinaryPlist(std::span<const uint8_t>(plain.data(), plain.size()))) {
            auto xml = Tools::bplistToXml(std::span<const uint8_t>(plain.data(), plain.size()));
            if (xml) {
                plain = std::move(*xml);
                if (bplistConvertedCount) {
                    ++*bplistConvertedCount;
                }
            }
        }
#endif

        auto write =
            Tools::writeWholeFile(path, std::span<const uint8_t>(plain.data(), plain.size()));
        if (!write) {
            continue;
        }

        ++decryptedCount;
        if (needsPngdefry && Tools::tryPngdefryInPlace(path))
            ++pngFixedCount;
    }
}

} // namespace

int main(int argc, char *argv[]) {
    argparse::ArgumentParser program("unjbt");
    program.add_description("List or extract .jbt/.orb/.rb archives (zip with BFCodec-encrypted "
                            "entries). Default is extract to current directory. Key/IV required "
                            "for extract, test, -c, -p.");

    program.add_argument("-K", "--key")
        .help("Passphrase (MD5-hashed to 16 bytes); default is the passphrase for jubeat Plus.");
    program.add_argument("--key-file")
        .help("Path to key file (first 16 bytes used; overrides default passphrase).");
    program.add_argument("--iv").help("IV as hex.");
    program.add_argument("--iv-file").help("Path to IV file (first 8 bytes used).");

    program.add_argument("-l")
        .help("List (short format).")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-Z")
        .help("List (zipinfo format).")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-t").help("Test archive.").default_value(false).implicit_value(true);
    program.add_argument("-c")
        .help("Extract to stdout with names.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-p")
        .help("Extract to stdout (data only).")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-f")
        .help("Freshen: extract only if newer.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-u")
        .help("Update: freshen + new files.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-n").help("Never overwrite.").default_value(false).implicit_value(true);
    program.add_argument("-j").help("Junk paths.").default_value(false).implicit_value(true);
    program.add_argument("-q").help("Quiet.").default_value(false).implicit_value(true);
    program.add_argument("-qq").help("Very quiet.").default_value(false).implicit_value(true);
    program.add_argument("-v").help("Verbose.").default_value(false).implicit_value(true);
    program.add_argument("-V")
        .help("Print version and exit.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-C")
        .help("Case-insensitive matching.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-LL")
        .help("Convert names to lowercase.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-W")
        .help("* and ? do not match /.")
        .default_value(false)
        .implicit_value(true);

    program.add_argument("-d", "--extract-to")
        .help("Extraction directory (default: .).")
        .default_value(std::string("."));
    program.add_argument("-x", "--exclude")
        .help("Exclude pattern (repeatable).")
        .nargs(argparse::nargs_pattern::at_least_one)
        .append();
    program.add_argument("-i", "--include")
        .help("Include pattern (repeatable).")
        .nargs(argparse::nargs_pattern::at_least_one)
        .append();

    program.add_argument("archive")
        .help("Archive file.")
        .nargs(argparse::nargs_pattern::optional)
        .default_value(std::string(""));
    program.add_argument("files").help("Optional member list.").nargs(argparse::nargs_pattern::any);

    spdlog::set_pattern("%v");

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception &err) {
        spdlog::error("{}", err.what());
        std::ostringstream oss;
        oss << program;
        spdlog::error("{}", oss.str());
        return EXIT_FAILURE;
    }

    Options opts;
    opts.listShort = program.get<bool>("-l");
    opts.listZipinfo = program.get<bool>("-Z");
    opts.test = program.get<bool>("-t");
    opts.extractStdout = program.get<bool>("-c");
    opts.pipeStdout = program.get<bool>("-p");
    opts.freshen = program.get<bool>("-f");
    opts.update = program.get<bool>("-u");
    opts.neverOverwrite = program.get<bool>("-n");
    opts.junkPaths = program.get<bool>("-j");
    opts.verbose = program.get<bool>("-v");
    opts.caseInsensitive = program.get<bool>("-C");
    opts.toLowercase = program.get<bool>("-LL");
    opts.wildcardNoSlash = program.get<bool>("-W");
    opts.exdir = fs::path(program.get<std::string>("--extract-to"));

    if (program.is_used("-x")) {
        for (const auto &group : program.get<std::vector<std::vector<std::string>>>("-x")) {
            for (const auto &p : group) {
                opts.excludePatterns.push_back(p);
            }
        }
    }
    if (program.is_used("-i")) {
        for (const auto &group : program.get<std::vector<std::vector<std::string>>>("-i")) {
            for (const auto &p : group) {
                opts.includePatterns.push_back(p);
            }
        }
    }
    auto files = program.get<std::vector<std::string>>("files");
    for (const auto &f : files) {
        opts.includePatterns.push_back(f);
    }

    opts.quiet = program.get<bool>("-qq") ? 2 : (program.get<bool>("-q") ? 1 : 0);

    fs::path archivePath(program.get<std::string>("archive"));

    if (program.get<bool>("-V")) {
        spdlog::info("unjbt v" BFCODEC_VERSION);
        return EXIT_SUCCESS;
    }

    if (archivePath.empty()) {
        spdlog::error("Archive required.");
        std::ostringstream oss;
        oss << program;
        spdlog::error("{}", oss.str());
        return EXIT_FAILURE;
    }

    if (opts.listShort || opts.listZipinfo) {
        int zipErr = 0;
        zip_t *zip = zip_open(archivePath.string().c_str(), ZIP_RDONLY, &zipErr);
        if (!zip) {
            spdlog::error("Open failed.");
            return EXIT_FAILURE;
        }
        if (opts.listZipinfo) {
            listZipinfo(zip, opts, archivePath);
        } else {
            listShort(zip, opts, archivePath);
        }
        zip_close(zip);
        return EXIT_SUCCESS;
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

    auto filter = [&opts](const std::string &name) { return shouldInclude(name, opts); };

    if (opts.test) {
        std::error_code ec;
        fs::path tmpDir = fs::temp_directory_path(ec) / "unjbt_test";
        if (ec) {
            spdlog::error("Cannot create temp dir.");
            return EXIT_FAILURE;
        }
        fs::create_directories(tmpDir, ec);
        auto result = extractZip(archivePath, tmpDir, opts, filter);
        if (!result) {
            spdlog::error("{}", message(result.error()));
            fs::remove_all(tmpDir, ec);
            return EXIT_FAILURE;
        }
        size_t decryptedCount = 0, skippedCount = 0, pngFixedCount = 0;
#ifdef HAVE_LIBPLIST
        size_t bplistConvertedCount = 0;
        decryptExtractedFiles(tmpDir,
                              *keyIv,
                              *codec,
                              decryptedCount,
                              skippedCount,
                              pngFixedCount,
                              &bplistConvertedCount,
                              opts.quiet);
#else
        decryptExtractedFiles(tmpDir,
                              *keyIv,
                              *codec,
                              decryptedCount,
                              skippedCount,
                              pngFixedCount,
                              nullptr,
                              opts.quiet);
#endif
        fs::remove_all(tmpDir, ec);
        if (opts.quiet < 2) {
            spdlog::info("No errors detected in archive.");
        }
        return EXIT_SUCCESS;
    }

    if (opts.extractStdout || opts.pipeStdout) {
        spdlog::error("-c/-p (extract to stdout) is not yet implemented.");
        return EXIT_FAILURE;
    }

    std::error_code ec;
    fs::create_directories(opts.exdir, ec);
    if (ec) {
        spdlog::error("Cannot create output directory: {}.", opts.exdir.string());
        return EXIT_FAILURE;
    }

    auto extractResult = extractZip(archivePath, opts.exdir, opts, filter);
    if (!extractResult) {
        spdlog::error("{}", message(extractResult.error()));
        return EXIT_FAILURE;
    }

    size_t decryptedCount = 0, skippedCount = 0, pngFixedCount = 0;
#ifdef HAVE_LIBPLIST
    size_t bplistConvertedCount = 0;
    decryptExtractedFiles(opts.exdir,
                          *keyIv,
                          *codec,
                          decryptedCount,
                          skippedCount,
                          pngFixedCount,
                          &bplistConvertedCount,
                          opts.quiet);
#else
    decryptExtractedFiles(opts.exdir,
                          *keyIv,
                          *codec,
                          decryptedCount,
                          skippedCount,
                          pngFixedCount,
                          nullptr,
                          opts.quiet);
#endif

    if (opts.quiet == 0) {
        std::string summary = "Extracted to " + opts.exdir.string() + ", decrypted " +
                              std::to_string(decryptedCount) + " file(s), skipped " +
                              std::to_string(skippedCount) + " file(s)";
        if (pngFixedCount > 0) {
            summary += ", fixed " + std::to_string(pngFixedCount) + " CgBI PNG(s)";
        }
#ifdef HAVE_LIBPLIST
        if (bplistConvertedCount > 0) {
            summary += ", converted " + std::to_string(bplistConvertedCount) + " bplist(s) to XML";
        }
#endif
        summary += ".";
        spdlog::info("{}", summary);
    }

    return EXIT_SUCCESS;
}
