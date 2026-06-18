#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bfcodecpp.h"

namespace Tools {

/** Read one entry's raw bytes from a zip archive by name. */
std::optional<std::vector<uint8_t>> readZipEntry(const std::filesystem::path &archivePath,
                                                 const char *entryName);

/**
 * Read the metadata entry from a package, preferring the newer @c infov2 over @c info. The two
 * share the same plist schema; newer jubeat packages ship only @c infov2, while older packages and
 * REFLEC BEAT packages use @c info. Returns the raw (still encrypted) bytes, or @c std::nullopt
 * when neither entry is present.
 */
std::optional<std::vector<uint8_t>> readInfoEntry(const std::filesystem::path &archivePath);

/**
 * Parse the BFCodec on-disk trailer that bfc appends ([fileSize:4 BE][blockSize:4 BE]).
 *
 * @param raw The whole entry, including the eight-byte trailer.
 * @return The pair @c {cipherLen, storedSize}, or @c std::nullopt when the buffer is shorter than
 * the trailer or the block size is not a positive multiple of eight.
 */
std::optional<std::pair<size_t, size_t>> bfcEntryLengths(std::span<const uint8_t> raw);

/**
 * Decrypt a single BFCodec entry buffer in place (CBC) using the trailer layout bfc writes.
 *
 * @param raw The whole entry, including the trailer.
 * @param codec A codec whose key has already been expanded.
 * @param iv The eight-byte initialisation vector.
 * @return The plaintext truncated to the stored size, or @c std::nullopt for a malformed or
 * undecryptable entry.
 */
std::optional<std::vector<uint8_t>>
decryptBfcEntry(const std::vector<uint8_t> &raw, BFCodec &codec, std::span<const std::byte, 8> iv);

/**
 * Extract every entry of a BFCodec archive into \a destDir, decrypting each with \a codec and \a iv.
 *
 * No format conversion is performed, so the written plaintext is byte-for-byte what was originally
 * packed (binary plists and CgBI PNGs are left intact). This makes it suitable for re-packing.
 *
 * @return @c true on success; on failure the cause is logged and @c false is returned.
 */
bool unpackArchive(const std::filesystem::path &archivePath,
                   const std::filesystem::path &destDir,
                   BFCodec &codec,
                   std::span<const std::byte, 8> iv);

/**
 * Pack every regular file under \a srcDir into a BFCodec archive at \a archivePath, encrypting each
 * with \a codec and \a iv (stored, not compressed).
 *
 * @return The number of files packed, or @c std::nullopt on failure (the cause is logged).
 */
std::optional<size_t> packArchive(const std::filesystem::path &srcDir,
                                  const std::filesystem::path &archivePath,
                                  BFCodec &codec,
                                  std::span<const std::byte, 8> iv);

/** Minimal XML element-content escaping for re-emitting libplist string values. */
std::string xmlEscape(std::string_view text);

/**
 * Build a mulist @c <dict> entry from a decrypted @c info plist.
 *
 * Name and Artist prefer the native field and fall back to the Roman reading and then Hira; ID is
 * taken verbatim; ItemURL is the akx-dl base joined with the archive's own filename (which carries
 * the per-song token). The returned text has no trailing newline.
 *
 * @param info The decrypted @c info entry (binary or XML plist).
 * @param archivePath The package whose filename forms the ItemURL.
 * @param error Set to the reason on failure (for example a malformed dictionary or missing libplist
 * support).
 * @return The entry text, or @c std::nullopt on failure.
 */
std::optional<std::string> buildMulistEntry(const std::vector<uint8_t> &info,
                                            const std::filesystem::path &archivePath,
                                            std::string &error);

} // namespace Tools
