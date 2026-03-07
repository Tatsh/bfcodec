#include "jbtcommon.h"

#include <spdlog/spdlog.h>

namespace Tools {

std::string message(ParseError e) {
    switch (e) {
    case ParseError::InvalidHexChar:
        return "Invalid hex character.";
    case ParseError::OddHexDigits:
        return "Odd number of hex digits.";
    }
    return "Unknown parse error.";
}

std::string message(FileError e) {
    switch (e) {
    case FileError::CannotOpen:
        return "Cannot open file.";
    case FileError::WrongSize:
        return "File size must be exactly required bytes.";
    case FileError::WriteFailed:
        return "Write failed.";
    }
    return "Unknown file error.";
}

std::string message(KeyIvError e) {
    switch (e) {
    case KeyIvError::KeyAndKeyFileBoth:
        return "Use -K/--key or --key-file, not both.";
    case KeyIvError::KeyRequired:
        return "Key is required (-K/--key or --key-file).";
    case KeyIvError::IvAndIvFileBoth:
        return "Use --iv or --iv-file, not both.";
    case KeyIvError::KeyEmpty:
        return "Key must not be empty.";
    case KeyIvError::KeyReadFailed:
        return "Cannot read key file.";
    case KeyIvError::KeyMd5Failed:
        return "Key: MD5 failed.";
    case KeyIvError::IvParseFailed:
        return "IV: invalid hex.";
    case KeyIvError::IvWrongSize:
        return "IV must be exactly 8 bytes (16 hex digits).";
    case KeyIvError::IvReadFailed:
        return "Cannot read IV file.";
    }
    return "Unknown key/IV error.";
}

std::string message(PlistError e) {
    switch (e) {
    case PlistError::FromBinFailed:
        return "plist_from_bin failed.";
    case PlistError::ToXmlFailed:
        return "plist_to_xml failed.";
    case PlistError::CFDataCreateFailed:
        return "CFDataCreate failed.";
    case PlistError::CFPropertyListCreateFailed:
        return "CFPropertyListCreateWithData failed.";
    case PlistError::CFPropertyListCreateDataFailed:
        return "CFPropertyListCreateData failed.";
    }
    return "Unknown plist error.";
}

int fromHexChar(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::expected<std::vector<std::byte>, ParseError> parseHex(std::string_view hex) {
    std::vector<std::byte> out;
    int nibble = -1;
    for (char c : hex) {
        if (std::isspace(static_cast<unsigned char>(c)))
            continue;
        int v = fromHexChar(c);
        if (v < 0) {
            return std::unexpected(ParseError::InvalidHexChar);
        }
        if (nibble < 0) {
            nibble = v;
        } else {
            out.push_back(static_cast<std::byte>((nibble << 4) | v));
            nibble = -1;
        }
    }
    if (nibble >= 0) {
        return std::unexpected(ParseError::OddHexDigits);
    }
    return out;
}

std::expected<std::vector<std::byte>, FileError> readFileExactly(const fs::path &path,
                                                                 size_t expectedSize) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return std::unexpected(FileError::CannotOpen);
    }
    std::vector<std::byte> buf(expectedSize);
    f.read(reinterpret_cast<char *>(buf.data()), static_cast<std::streamsize>(expectedSize));
    if (!f || f.get() != std::char_traits<char>::eof()) {
        return std::unexpected(FileError::WrongSize);
    }
    return buf;
}

std::expected<std::array<std::byte, 16>, KeyIvError> md5Key(std::span<const std::byte> data) {
    std::array<std::byte, 16> out{};
#if defined(__APPLE__)
    CC_MD5(data.data(),
           static_cast<CC_LONG>(data.size()),
           reinterpret_cast<unsigned char *>(out.data()));
    return out;
#elif defined(_WIN32)
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        return std::unexpected(KeyIvError::KeyMd5Failed);
    }
    if (!CryptCreateHash(prov, CALG_MD5, 0, 0, &hash)) {
        CryptReleaseContext(prov, 0);
        return std::unexpected(KeyIvError::KeyMd5Failed);
    }
    if (!CryptHashData(hash,
                       reinterpret_cast<const BYTE *>(data.data()),
                       static_cast<DWORD>(data.size()),
                       0)) {
        CryptDestroyHash(hash);
        CryptReleaseContext(prov, 0);
        return std::unexpected(KeyIvError::KeyMd5Failed);
    }
    DWORD len = 16;
    if (!CryptGetHashParam(hash, HP_HASHVAL, reinterpret_cast<BYTE *>(out.data()), &len, 0)) {
        CryptDestroyHash(hash);
        CryptReleaseContext(prov, 0);
        return std::unexpected(KeyIvError::KeyMd5Failed);
    }
    CryptDestroyHash(hash);
    CryptReleaseContext(prov, 0);
    return out;
#else
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        return std::unexpected(KeyIvError::KeyMd5Failed);
    }
    if (EVP_DigestInit_ex(ctx, EVP_md5(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, data.data(), data.size()) != 1 ||
        EVP_DigestFinal_ex(ctx, reinterpret_cast<unsigned char *>(out.data()), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        return std::unexpected(KeyIvError::KeyMd5Failed);
    }
    EVP_MD_CTX_free(ctx);
    return out;
#endif
}

std::expected<KeyIv, KeyIvError> getKeyIv(argparse::ArgumentParser &program) {
    auto keyOpt = program.present<std::string>("--key");
    auto keyFileOpt = program.present<std::string>("--key-file");
    auto ivOpt = program.present<std::string>("--iv");
    auto ivFileOpt = program.present<std::string>("--iv-file");

    if (keyOpt && keyFileOpt) {
        return std::unexpected(KeyIvError::KeyAndKeyFileBoth);
    }
    if (!keyOpt && !keyFileOpt) {
        return std::unexpected(KeyIvError::KeyRequired);
    }
    if (ivOpt && ivFileOpt) {
        return std::unexpected(KeyIvError::IvAndIvFileBoth);
    }

    KeyIv out;

    if (keyOpt) {
        if (keyOpt->empty()) {
            return std::unexpected(KeyIvError::KeyEmpty);
        }
        std::span<const std::byte> passphrase(reinterpret_cast<const std::byte *>(keyOpt->data()),
                                              keyOpt->size());
        auto digest = md5Key(passphrase);
        if (!digest) {
            return std::unexpected(digest.error());
        }
        out.keyBytes.assign(digest->begin(), digest->end());
    } else {
        auto read = readFileExactly(*keyFileOpt, 16);
        if (!read) {
            return std::unexpected(KeyIvError::KeyReadFailed);
        }
        out.keyBytes = std::move(*read);
    }

    if (ivOpt) {
        auto parsed = parseHex(*ivOpt);
        if (!parsed) {
            return std::unexpected(KeyIvError::IvParseFailed);
        }
        if (parsed->size() != 8) {
            return std::unexpected(KeyIvError::IvWrongSize);
        }
        std::copy_n(parsed->begin(), 8, out.ivBytes.begin());
    } else if (ivFileOpt) {
        auto read = readFileExactly(*ivFileOpt, 8);
        if (!read) {
            return std::unexpected(KeyIvError::IvReadFailed);
        }
        std::copy_n(read->begin(), 8, out.ivBytes.begin());
    } else {
        out.ivBytes = kDefaultIv;
    }
    return out;
}

std::expected<BFCodec, BFCodecError> createCodec(const std::vector<std::byte> &keyBytes) {
    auto codec = BFCodec::create();
    if (!codec) {
        return std::unexpected(codec.error());
    }
    auto expand = codec->expandKey(keyBytes);
    if (!expand) {
        return std::unexpected(expand.error());
    }
    return std::move(*codec);
}

std::expected<std::vector<uint8_t>, FileError> readWholeFile(const fs::path &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return std::unexpected(FileError::CannotOpen);
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    return data;
}

std::expected<void, FileError> writeWholeFile(const fs::path &path, std::span<const uint8_t> data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        return std::unexpected(FileError::CannotOpen);
    }
    f.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!f) {
        return std::unexpected(FileError::WriteFailed);
    }
    return {};
}

bool isBinaryPlist(std::span<const uint8_t> data) {
    static constexpr std::array<uint8_t, 6> kMagic = {'b', 'p', 'l', 'i', 's', 't'};
    if (data.size() < kMagic.size()) {
        return false;
    }
    return std::equal(kMagic.begin(), kMagic.end(), data.begin());
}

#if defined(HAVE_COREFOUNDATION)
std::expected<std::vector<uint8_t>, PlistError> bplistToXml(std::span<const uint8_t> data) {
    CFDataRef cfData = CFDataCreate(kCFAllocatorDefault,
                                    reinterpret_cast<const UInt8 *>(data.data()),
                                    static_cast<CFIndex>(data.size()));
    if (!cfData) {
        return std::unexpected(PlistError::CFDataCreateFailed);
    }

    CFErrorRef error = nullptr;
    CFPropertyListRef plist = CFPropertyListCreateWithData(
        kCFAllocatorDefault, cfData, kCFPropertyListImmutable, nullptr, &error);
    CFRelease(cfData);
    if (!plist) {
        if (error) {
            CFRelease(error);
        }
        return std::unexpected(PlistError::CFPropertyListCreateFailed);
    }

    CFDataRef xmlData = CFPropertyListCreateData(
        kCFAllocatorDefault, plist, kCFPropertyListXMLFormat_v1_0, 0, &error);
    CFRelease(plist);
    if (!xmlData) {
        if (error) {
            CFRelease(error);
        }
        return std::unexpected(PlistError::CFPropertyListCreateDataFailed);
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
std::expected<std::vector<uint8_t>, PlistError> bplistToXml(std::span<const uint8_t> data) {
    plist_t root = nullptr;
    plist_err_t err = plist_from_bin(
        reinterpret_cast<const char *>(data.data()), static_cast<uint32_t>(data.size()), &root);
    if (err != PLIST_ERR_SUCCESS || !root) {
        return std::unexpected(PlistError::FromBinFailed);
    }

    char *xml = nullptr;
    uint32_t length = 0;
    err = plist_to_xml(root, &xml, &length);
    if (err != PLIST_ERR_SUCCESS || !xml) {
        plist_free(root);
        return std::unexpected(PlistError::ToXmlFailed);
    }
    std::vector<uint8_t> out(reinterpret_cast<uint8_t *>(xml),
                             reinterpret_cast<uint8_t *>(xml) + length);
    plist_mem_free(xml);
    plist_free(root);
    return out;
}
#endif

bool isPngData(std::span<const uint8_t> data) {
    static constexpr std::array<uint8_t, 8> kPngSig = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (data.size() < kPngSig.size()) {
        return false;
    }
    return std::equal(kPngSig.begin(), kPngSig.end(), data.begin());
}

bool containsCgbi(std::span<const uint8_t> data) {
    static constexpr std::array<uint8_t, 4> kCgbi = {'C', 'g', 'B', 'I'};
    if (data.size() < kCgbi.size()) {
        return false;
    }

    const size_t searchLimit = std::min<size_t>(data.size(), 1024 * 1024);
    auto begin = data.begin();
    auto end = data.begin() + static_cast<std::ptrdiff_t>(searchLimit);
    return std::search(begin, end, kCgbi.begin(), kCgbi.end()) != end;
}

int runProcess(const std::vector<std::string> &argv) {
    if (argv.empty()) {
        return -1;
    }

#ifndef _WIN32
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
#else
    // Build command line: quote each argument and join with spaces.
    std::string cmdLine;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i != 0) {
            cmdLine += ' ';
        }
        cmdLine += '"';
        for (char c : argv[i]) {
            if (c == '"') {
                cmdLine += "\\\"";
            } else {
                cmdLine += c;
            }
        }
        cmdLine += '"';
    }
    // Convert UTF-8 to wide; CreateProcessW requires a writable buffer.
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), -1, nullptr, 0);
    if (wideLen <= 0) {
        return -1;
    }
    std::vector<wchar_t> cmdLineW(static_cast<size_t>(wideLen));
    if (MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), -1, cmdLineW.data(), wideLen) == 0) {
        return -1;
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(
            nullptr, cmdLineW.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        return -1;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    return static_cast<int>(exitCode);
#endif
}

bool tryPngdefryInPlace(const fs::path &pngPath) {
#ifdef __APPLE__
    // Prefer CoreGraphics-compatible fix via pngcrush on macOS when available.
    fs::path tmp = pngPath;
    tmp += ".pngfix";
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

#ifndef _WIN32
    std::error_code ec;
    fs::path tmpBase = fs::temp_directory_path(ec);
    if (ec) {
        return false;
    }
    std::string tmpTemplate = (tmpBase / "pngdefry_XXXXXX").string();
    std::vector<char> buf(tmpTemplate.begin(), tmpTemplate.end());
    buf.push_back('\0');
    if (!mkdtemp(buf.data())) {
        return false;
    }
    fs::path tmpDir = buf.data();
    spdlog::debug("Created temporary directory: {}.", tmpDir.string());

    int rc = runProcess({"pngdefry", "-o", tmpDir.string(), pngPath.string()});
    if (rc != 0) {
        fs::remove_all(tmpDir, ec);
        return false;
    }
    fs::path outFile = tmpDir / pngPath.filename();
    if (!fs::exists(outFile)) {
        fs::remove_all(tmpDir, ec);
        return false;
    }
    bool moved = false;
    fs::rename(outFile, pngPath, ec);
    if (!ec) {
        moved = true;
    } else {
        ec.clear();
        fs::copy_file(outFile, pngPath, fs::copy_options::overwrite_existing, ec);
        if (!ec) {
            moved = true;
            fs::remove(outFile, ec);
        }
    }
    fs::remove_all(tmpDir, ec);
    return moved;
#else
    // Windows: get temp dir via API, create unique subdir, same pngdefry flow
    wchar_t tempPathBuf[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tempPathBuf) == 0) {
        return false;
    }
    wchar_t uniquePathBuf[MAX_PATH];
    if (GetTempFileNameW(tempPathBuf, L"pngdefry", 0, uniquePathBuf) == 0) {
        return false;
    }
    if (!DeleteFileW(uniquePathBuf)) {
        return false;
    }
    if (!CreateDirectoryW(uniquePathBuf, nullptr)) {
        return false;
    }
    fs::path tmpDir(uniquePathBuf);

    int rc = runProcess({"pngdefry", "-o", tmpDir.string(), pngPath.string()});
    if (rc != 0) {
        std::error_code ec;
        fs::remove_all(tmpDir, ec);
        return false;
    }
    fs::path outFile = tmpDir / pngPath.filename();
    if (!fs::exists(outFile)) {
        std::error_code ec;
        fs::remove_all(tmpDir, ec);
        return false;
    }
    std::error_code ec;
    bool moved = false;
    fs::rename(outFile, pngPath, ec);
    if (!ec) {
        moved = true;
    } else {
        ec.clear();
        fs::copy_file(outFile, pngPath, fs::copy_options::overwrite_existing, ec);
        if (!ec) {
            moved = true;
            fs::remove(outFile, ec);
        }
    }
    fs::remove_all(tmpDir, ec);
    return moved;
#endif
}

fs::path safeJoinZipPath(const fs::path &root, const std::string &zipName) {
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

} // namespace Tools
