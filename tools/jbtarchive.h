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
 * Compute the mulist @c holdFlag for a jubeat package: a bit per difficulty that has hold notes,
 * 1 for basic, 2 for advanced, and 4 for extreme.
 *
 * This is what the game itself records after a download. It decrypts @c seq_bas , @c seq_adv , and
 * @c seq_ext with the same key the metadata entry uses, requires the @c IJSQ chart magic, and walks
 * the eight-byte event records for an event whose kind byte is 6. A chart carrying either of the
 * other two accepted magics, @c IJBQ or @c JBSQ , is reported as hold-free without its events being
 * read, which matches the game and is not a shortcut: no chart in either of those formats carries a
 * hold event.
 *
 * A converted package can ship its charts unencrypted, so an entry with no BFCodec trailer is read
 * as a chart directly rather than skipped. The magic check applies either way, which is what makes
 * that safe.
 *
 * Logs the difficulties it found at info level, and warns when no chart could be read at all: a
 * package whose charts are in an unexpected format would otherwise be indistinguishable from one
 * that genuinely has no holds.
 *
 * @param archivePath The package to inspect.
 * @param codec A codec whose key has already been expanded.
 * @param iv The eight-byte initialisation vector.
 * @return The bitmask, which is zero when the package has no charts, none of them is readable, or
 * none contains a hold event.
 */
unsigned int holdMarkerFlag(const std::filesystem::path &archivePath,
                            BFCodec &codec,
                            std::span<const std::byte, 8> iv);

/**
 * Build a mulist @c <dict> entry from a decrypted @c info plist.
 *
 * Name and Artist prefer the native field and fall back to the Roman reading and then Hira; ID is
 * taken verbatim; ItemURL is the akx-dl base joined with the archive's own filename (which carries
 * the per-song token). The returned text has no trailing newline.
 *
 * @param info The decrypted @c info entry (binary or XML plist).
 * @param archivePath The package whose filename forms the ItemURL.
 * @param holdFlag The mask from @c holdMarkerFlag ; written only for a jubeat package, whose
 * metadata schema is recognised by its @c Name key. Pass 0 for a package that has no charts to
 * inspect.
 * @param error Set to the reason on failure (for example a malformed dictionary or missing libplist
 * support).
 * @return The entry text, or @c std::nullopt on failure.
 */
std::optional<std::string> buildMulistEntry(const std::vector<uint8_t> &info,
                                            const std::filesystem::path &archivePath,
                                            unsigned int holdFlag,
                                            std::string &error);

} // namespace Tools
