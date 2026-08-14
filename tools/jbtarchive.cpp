#include "jbtarchive.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>

#include <spdlog/spdlog.h>
#include <zip.h>

#include "jbtcommon.h"

namespace Tools {

std::optional<std::vector<uint8_t>> readZipEntry(const std::filesystem::path &archivePath,
                                                 const char *entryName) {
    int zipErr = 0;
    zip_t *zip = zip_open(archivePath.string().c_str(), ZIP_RDONLY, &zipErr);
    if (!zip) {
        return std::nullopt;
    }
    const zip_int64_t idx = zip_name_locate(zip, entryName, 0);
    if (idx < 0) {
        zip_close(zip);
        return std::nullopt;
    }
    zip_stat_t st;
    zip_stat_init(&st);
    if (zip_stat_index(zip, static_cast<zip_uint64_t>(idx), 0, &st) < 0) {
        zip_close(zip);
        return std::nullopt;
    }
    zip_file_t *zf = zip_fopen_index(zip, static_cast<zip_uint64_t>(idx), 0);
    if (!zf) {
        zip_close(zip);
        return std::nullopt;
    }
    std::vector<uint8_t> buf(st.size);
    zip_uint64_t total = 0;
    while (total < st.size) {
        const zip_int64_t nr = zip_fread(zf, buf.data() + total, st.size - total);
        if (nr <= 0) {
            zip_fclose(zf);
            zip_close(zip);
            return std::nullopt;
        }
        total += static_cast<zip_uint64_t>(nr);
    }
    zip_fclose(zf);
    zip_close(zip);
    return buf;
}

std::optional<std::vector<uint8_t>> readInfoEntry(const std::filesystem::path &archivePath) {
    // Newer jubeat packages ship only infov2; it has the same schema as info and takes precedence
    // when both are present.
    if (auto raw = readZipEntry(archivePath, "infov2")) {
        return raw;
    }
    return readZipEntry(archivePath, "info");
}

bool hasInfoV2Entry(const std::filesystem::path &archivePath) {
    return readZipEntry(archivePath, "infov2").has_value();
}

std::optional<std::pair<size_t, size_t>> bfcEntryLengths(std::span<const uint8_t> raw) {
    if (raw.size() < 8) {
        return std::nullopt;
    }
    const size_t trailerOffset = raw.size() - 8u;
    std::span<const uint8_t> trailer = raw.subspan(trailerOffset, 8u);
    const uint32_t fileSize =
        (static_cast<uint32_t>(trailer[0]) << 24) | (static_cast<uint32_t>(trailer[1]) << 16) |
        (static_cast<uint32_t>(trailer[2]) << 8) | static_cast<uint32_t>(trailer[3]);
    const uint32_t blockSize =
        (static_cast<uint32_t>(trailer[4]) << 24) | (static_cast<uint32_t>(trailer[5]) << 16) |
        (static_cast<uint32_t>(trailer[6]) << 8) | static_cast<uint32_t>(trailer[7]);
    const size_t cipherLen = std::min<size_t>(blockSize, trailerOffset);
    if (cipherLen == 0 || (cipherLen % 8u) != 0u) {
        return std::nullopt;
    }
    const size_t storedSize = std::min<size_t>(fileSize, cipherLen);
    return std::pair<size_t, size_t>{cipherLen, storedSize};
}

std::optional<std::vector<uint8_t>>
decryptBfcEntry(const std::vector<uint8_t> &raw, BFCodec &codec, std::span<const std::byte, 8> iv) {
    auto lengths = bfcEntryLengths(std::span<const uint8_t>(raw.data(), raw.size()));
    if (!lengths) {
        return std::nullopt;
    }
    const auto [cipherLen, storedSize] = *lengths;
    std::vector<uint8_t> plain(cipherLen);
    std::copy_n(raw.begin(), cipherLen, plain.begin());
    std::span<std::byte> span = std::as_writable_bytes(std::span(plain).first(cipherLen));
    auto result = codec.decrypt(span, iv);
    if (!result) {
        return std::nullopt;
    }
    plain.resize(storedSize);
    return plain;
}

bool unpackArchive(const std::filesystem::path &archivePath,
                   const std::filesystem::path &destDir,
                   BFCodec &codec,
                   std::span<const std::byte, 8> iv) {
    int zipErr = 0;
    zip_t *zip = zip_open(archivePath.string().c_str(), ZIP_RDONLY, &zipErr);
    if (!zip) {
        spdlog::error("Cannot open archive: {}.", archivePath.string());
        return false;
    }
    const zip_int64_t n = zip_get_num_entries(zip, 0);
    for (zip_int64_t i = 0; i < n; ++i) {
        const char *name = zip_get_name(zip, static_cast<zip_uint64_t>(i), 0);
        if (!name) {
            spdlog::error("Cannot read an entry name from archive.");
            zip_close(zip);
            return false;
        }
        const fs::path outPath = safeJoinZipPath(destDir, name);
        if (outPath.empty()) {
            spdlog::error("Unsafe path in archive: {}.", name);
            zip_close(zip);
            return false;
        }
        const std::string_view entryName(name);
        if (!entryName.empty() && entryName.back() == '/') {
            std::error_code ec;
            fs::create_directories(outPath, ec);
            if (ec) {
                spdlog::error("Cannot create directory: {}.", outPath.string());
                zip_close(zip);
                return false;
            }
            continue;
        }
        zip_stat_t st;
        zip_stat_init(&st);
        if (zip_stat_index(zip, static_cast<zip_uint64_t>(i), 0, &st) < 0) {
            spdlog::error("Cannot stat entry: {}.", name);
            zip_close(zip);
            return false;
        }
        zip_file_t *zf = zip_fopen_index(zip, static_cast<zip_uint64_t>(i), 0);
        if (!zf) {
            spdlog::error("Cannot read entry: {}.", name);
            zip_close(zip);
            return false;
        }
        std::vector<uint8_t> raw(st.size);
        zip_uint64_t total = 0;
        while (total < st.size) {
            const zip_int64_t nr = zip_fread(zf, raw.data() + total, st.size - total);
            if (nr <= 0) {
                spdlog::error("Cannot read entry: {}.", name);
                zip_fclose(zf);
                zip_close(zip);
                return false;
            }
            total += static_cast<zip_uint64_t>(nr);
        }
        zip_fclose(zf);

        auto plain = decryptBfcEntry(raw, codec, iv);
        if (!plain) {
            spdlog::error("Cannot decrypt entry: {}.", name);
            zip_close(zip);
            return false;
        }

        std::error_code ec;
        fs::create_directories(outPath.parent_path(), ec);
        if (ec) {
            spdlog::error("Cannot create directory: {}.", outPath.parent_path().string());
            zip_close(zip);
            return false;
        }
        auto write =
            writeWholeFile(outPath, std::span<const uint8_t>(plain->data(), plain->size()));
        if (!write) {
            spdlog::error("{}", message(write.error()));
            zip_close(zip);
            return false;
        }
    }
    zip_close(zip);
    return true;
}

std::optional<size_t> packArchive(const std::filesystem::path &srcDir,
                                  const std::filesystem::path &archivePath,
                                  BFCodec &codec,
                                  std::span<const std::byte, 8> iv) {
    int zipErr = 0;
    zip_t *zip = zip_open(archivePath.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipErr);
    if (!zip) {
        zip_error_t err;
        zip_error_init_with_code(&err, zipErr);
        spdlog::error("{}", zip_error_strerror(&err));
        zip_error_fini(&err);
        return std::nullopt;
    }

    size_t encryptedCount = 0;
    for (auto it = fs::recursive_directory_iterator(srcDir);
         it != fs::recursive_directory_iterator();
         ++it) {
        if (!it->is_regular_file()) {
            continue;
        }
        const fs::path filePath = it->path();
        const std::string zipName = fs::relative(filePath, srcDir).generic_string();

        auto plain = readWholeFile(filePath);
        if (!plain) {
            spdlog::error("{}", message(plain.error()));
            zip_close(zip);
            return std::nullopt;
        }

        const uint32_t fileSize = static_cast<uint32_t>(plain->size());
        std::vector<uint8_t> buf = std::move(*plain);
        const size_t paddedSize = (buf.size() + 7u) / 8u * 8u;
        buf.resize(paddedSize, 0);
        const uint32_t blockSize = static_cast<uint32_t>(buf.size());
        if (blockSize == 0 || (buf.size() % 8u) != 0u) {
            spdlog::error("Invalid block size for {}.", filePath.string());
            zip_close(zip);
            return std::nullopt;
        }

        std::span<std::byte> span = std::as_writable_bytes(std::span(buf));
        auto result = codec.encrypt(span, iv);
        if (!result) {
            spdlog::error("{} for {}.", BFCodec::message(result.error()), filePath.string());
            zip_close(zip);
            return std::nullopt;
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
            return std::nullopt;
        }
        std::copy(out.begin(), out.end(), copy);

        zip_source_t *src = zip_source_buffer(zip, copy, out.size(), 1);
        if (!src) {
            spdlog::error("{}", zip_strerror(zip));
            free(copy);
            zip_close(zip);
            return std::nullopt;
        }

        const zip_int64_t index = zip_file_add(zip, zipName.c_str(), src, ZIP_FL_OVERWRITE);
        if (index < 0) {
            spdlog::error("{}", zip_strerror(zip));
            zip_source_free(src);
            zip_close(zip);
            return std::nullopt;
        }
        if (zip_set_file_compression(zip, static_cast<zip_uint64_t>(index), ZIP_CM_STORE, 0) < 0) {
            spdlog::error("{}", zip_strerror(zip));
            zip_close(zip);
            return std::nullopt;
        }
        ++encryptedCount;
    }

    if (zip_close(zip) < 0) {
        spdlog::error("Close failed.");
        return std::nullopt;
    }
    return encryptedCount;
}

std::string xmlEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

namespace {

// The chart entries a jubeat package carries, paired with the holdFlag bit each one sets.
constexpr std::array<std::pair<const char *, unsigned int>, 3> kChartEntries = {
    {{"seq_bas", 1}, {"seq_adv", 2}, {"seq_ext", 4}}};

// Only a chart in the IJSQ format is examined; see holdMarkerFlag's documentation.
constexpr std::array<uint8_t, 4> kHoldChartMagic = {'I', 'J', 'S', 'Q'};

// The difficulty each holdFlag bit stands for, for logging.
constexpr std::array<std::pair<const char *, unsigned int>, 3> kDifficultyNames = {
    {{"basic", 1}, {"advanced", 2}, {"extreme", 4}}};
// The header is 0x60 bytes and the event count is the word after the magic. Each event record that
// follows is eight bytes whose first byte is the kind.
constexpr size_t kChartHeaderSize = 0x60;
constexpr size_t kChartEventCountOffset = 4;
constexpr size_t kChartEventSize = 8;
constexpr uint8_t kChartEventHold = 6;

// Whether the buffer opens with the chart magic that carries hold events. The other two magics the
// game's loader accepts, IJBQ and JBSQ, are chart formats too, but its hold check rejects them
// before reading any event and no chart in either format carries one.
bool hasChartMagic(const std::vector<uint8_t> &chart) {
    return chart.size() >= kChartHeaderSize &&
           std::equal(kHoldChartMagic.begin(), kHoldChartMagic.end(), chart.begin());
}

// Render a holdFlag as the difficulties it names, for a log line.
std::string describeDifficulties(unsigned int flag) {
    std::string out;
    for (const auto &[name, bit] : kDifficultyNames) {
        if ((flag & bit) != 0) {
            if (!out.empty()) {
                out += '+';
            }
            out += name;
        }
    }
    return out;
}

bool chartHasHold(const std::vector<uint8_t> &chart) {
    if (!hasChartMagic(chart)) {
        return false;
    }
    uint32_t eventCount = 0;
    std::memcpy(&eventCount, chart.data() + kChartEventCountOffset, sizeof(eventCount));
    if (eventCount == 0) {
        return false;
    }
    // The game sizes this bound in 32 bits and lets the product wrap, so a chart claiming an absurd
    // event count is rejected by the length check rather than trusted.
    const uint32_t required = static_cast<uint32_t>(eventCount * kChartEventSize) +
                              static_cast<uint32_t>(kChartHeaderSize);
    if (chart.size() < required) {
        return false;
    }
    for (uint32_t i = 0; i < eventCount; ++i) {
        if (chart[kChartHeaderSize + i * kChartEventSize] == kChartEventHold) {
            return true;
        }
    }
    return false;
}

} // namespace

unsigned int holdMarkerFlag(const std::filesystem::path &archivePath,
                            BFCodec &codec,
                            std::span<const std::byte, 8> iv) {
    unsigned int flag = 0;
    unsigned int examined = 0;
    for (const auto &[entryName, bit] : kChartEntries) {
        auto raw = readZipEntry(archivePath, entryName);
        if (!raw) {
            continue;
        }
        // A converted package can ship its charts unencrypted, in which case there is no trailer to
        // decrypt and the raw bytes already are the chart. Falling back to them is safe because the
        // magic is checked either way, and ciphertext that happens to begin with the chart magic is
        // not a case worth guarding against.
        auto decrypted = decryptBfcEntry(*raw, codec, iv);
        const std::vector<uint8_t> &chart = decrypted ? *decrypted : *raw;
        if (!hasChartMagic(chart)) {
            continue;
        }
        ++examined;
        if (chartHasHold(chart)) {
            flag |= bit;
        }
    }
    if (examined == 0) {
        spdlog::warn("{}: no chart could be read, so its holdFlag is reported as 0 rather than "
                     "determined. A wrong key or an unexpected chart format would look like this.",
                     archivePath.filename().string());
    } else if (flag != 0) {
        spdlog::info("{}: holds in {} (holdFlag {}).",
                     archivePath.filename().string(),
                     describeDifficulties(flag),
                     flag);
    } else {
        spdlog::debug(
            "{}: no holds in any of its {} chart(s).", archivePath.filename().string(), examined);
    }
    return flag;
}

std::optional<std::string> buildMulistEntry(const std::vector<uint8_t> &info,
                                            const std::filesystem::path &archivePath,
                                            unsigned int holdFlag,
                                            std::string &error) {
#ifdef HAVE_LIBPLIST
    plist_t root = nullptr;
    const auto *chars = reinterpret_cast<const char *>(info.data());
    const auto length = static_cast<uint32_t>(info.size());
    if (isBinaryPlist(std::span<const uint8_t>(info.data(), info.size()))) {
        plist_from_bin(chars, length, &root);
    } else {
        plist_from_xml(chars, length, &root);
    }
    if (!root || plist_get_node_type(root) != PLIST_DICT) {
        if (root) {
            plist_free(root);
        }
        error = "'info' is not a plist dictionary.";
        return std::nullopt;
    }
    auto stringForKey = [root](const char *key) -> std::string {
        plist_t node = plist_dict_get_item(root, key);
        if (!node || plist_get_node_type(node) != PLIST_STRING) {
            return {};
        }
        char *value = nullptr;
        plist_get_string_val(node, &value);
        std::string out = value ? value : "";
        if (value) {
            plist_mem_free(value);
        }
        return out;
    };
    auto firstNonEmpty = [&stringForKey](std::initializer_list<const char *> keys) {
        for (const char *key : keys) {
            std::string value = stringForKey(key);
            if (!value.empty()) {
                return value;
            }
        }
        return std::string();
    };
    // Artist and Name use the native form the on-device mulist stores. REFLEC BEAT exposes them as
    // ArtistName / MusicName (with Roman and Hira readings); jubeat (jbt) uses Artist / Name. jbt's
    // NameYomi is a phonetic reading for Japanese sorting, not a display title, so it is not used
    // here. Both key schemes are tried, so either package type populates the entry.
    const std::string artist =
        firstNonEmpty({"ArtistName", "ArtistNameRoman", "ArtistNameHira", "Artist"});
    const std::string name =
        firstNonEmpty({"MusicName", "MusicNameRoman", "MusicNameHira", "Name"});
    plist_t idNode = plist_dict_get_item(root, "ID");
    if (!idNode || plist_get_node_type(idNode) != PLIST_UINT) {
        plist_free(root);
        error = "'info' has no integer ID key.";
        return std::nullopt;
    }
    uint64_t id = 0;
    plist_get_uint_val(idNode, &id);
    // jubeat info uses Name/Artist; REFLEC BEAT uses MusicName/ArtistName. Only jubeat mulist
    // entries carry holdFlag, detected from the info schema before the dict is freed.
    const bool isJubeat = plist_dict_get_item(root, "Name") != nullptr &&
                          plist_dict_get_item(root, "MusicName") == nullptr;
    plist_free(root);
    const std::string url = "https://akx-dl.konami.net/akx/data/" + archivePath.filename().string();
    std::string entry = "\t<dict>\n";
    entry += "\t\t<key>Artist</key>\n\t\t<string>" + xmlEscape(artist) + "</string>\n";
    entry += "\t\t<key>ID</key>\n\t\t<integer>" + std::to_string(id) + "</integer>\n";
    entry += "\t\t<key>ItemURL</key>\n\t\t<string>" + xmlEscape(url) + "</string>\n";
    entry += "\t\t<key>Name</key>\n\t\t<string>" + xmlEscape(name) + "</string>\n";
    // jubeat requires holdFlag: a 3-bit mask, one bit per difficulty (Basic, Advanced, Extreme),
    // set when that difficulty has hold notes. The caller reads it out of the package's own charts
    // with holdMarkerFlag, which is what the game does after a download.
    if (isJubeat) {
        entry +=
            "\t\t<key>holdFlag</key>\n\t\t<integer>" + std::to_string(holdFlag) + "</integer>\n";
    }
    entry += "\t</dict>";
    return entry;
#else
    (void)info;
    (void)archivePath;
    error = "libplist support is required to read the 'info' entry.";
    return std::nullopt;
#endif
}

} // namespace Tools
