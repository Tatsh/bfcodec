#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <argparse/argparse.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "jbtarchive.h"
#include "jbtcommon.h"

namespace fs = std::filesystem;

namespace {

// Property-list envelope wrapped around the collected <dict> entries to form a mulist document.
const std::string kMulistHeader = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                  "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                                  "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                                  "<plist version=\"1.0\">\n"
                                  "<array>\n";
const std::string kMulistFooter = "</array>\n</plist>\n";

// Root of the per-application data containers on a jailbroken device. The app-specific UUID and the
// Documents / Library subdirectories are appended to this.
const std::string kRemoteContainerRoot = "/var/mobile/Containers/Data/Application";

// REFLEC BEAT plus keys that need special handling. The Japanese DLC key is what the supported app
// (jp.konami.rbplus) reads; world-version packages keyed with the lovers key are re-encrypted to it.
// The regular key decrypts DecodeType 0 (bundled) content.
const std::string kReflecRegularKey = "Konami ReflecBeat For iOS.";
const std::string kReflecJpDlcKey = "Konami ReflecBeatplus.";
const std::string kReflecWorldKey = "REFLECBEATplus lovers.";

// Authentication options for the default scp transport, used when --rsync is not given. -F /dev/null
// ignores the user's ssh_config while password and keyboard-interactive authentication are enabled;
// public-key authentication stays first so existing keys are still used without a prompt.
const std::vector<std::string> kSshAuthOptions = {
    "-F",
    "/dev/null",
    "-o",
    "KbdInteractiveAuthentication=yes",
    "-o",
    "PreferredAuthentications=publickey,password,keyboard-interactive",
    "-o",
    "PasswordAuthentication=yes"};

// Authentication options used when --rsync is given. rsync launches its own ssh with no controlling
// terminal, so a password prompt can never be answered; BatchMode=yes makes that failure immediate,
// and public-key is the only permitted method. Unlike kSshAuthOptions this omits -F /dev/null, so
// the user's ssh_config is honoured.
const std::vector<std::string> kKeyOnlySshOptions = {"-o",
                                                     "BatchMode=yes",
                                                     "-o",
                                                     "PreferredAuthentications=publickey",
                                                     "-o",
                                                     "PasswordAuthentication=no",
                                                     "-o",
                                                     "KbdInteractiveAuthentication=no"};

std::string toLowerAscii(std::string s) {
    for (char &c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// Return the passphrases to try for the given package type. The trailing dots in the REFLEC BEAT
// keys are part of the passphrase, not punctuation.
std::vector<std::string> candidateKeys(const std::string &type) {
    if (type == "jbt") {
        return {"Konami Bemani Mobile iPad", "jubeatskmpledata", "Konami Bemani Mobile iOS"};
    }
    return {kReflecRegularKey, kReflecJpDlcKey, kReflecWorldKey};
}

// A goodrb.json entry, reduced to the fields deploy-rb consults when deciding how to treat a
// package.
struct DatabaseEntry {
    std::string id;
    int keyType = 0;
    bool world = false;
    bool bundled = false;
    std::optional<std::string> jpnId;
    // When set, this entry is a newer revision of the entry with this SHA-256; revisions are
    // preferred over their originals when two inputs map to the same on-device name.
    bool isRevision = false;
};

// The goodrb.json database indexed by file SHA-256 (lowercase hex), which is the authority for
// matching an input package to a known song.
struct Database {
    std::unordered_map<std::string, DatabaseEntry> bySha;
};

// Load the goodrb.json database and index every entry by its SHA-256. Returns nullopt on a read or
// parse error, having already logged the cause.
std::optional<Database> loadDatabase(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        spdlog::error("Cannot open database {}.", path.string());
        return std::nullopt;
    }
    nlohmann::json doc;
    try {
        in >> doc;
    } catch (const std::exception &err) {
        spdlog::error("Cannot parse database {}: {}.", path.string(), err.what());
        return std::nullopt;
    }
    const auto songs = doc.find("songs");
    if (songs == doc.end() || !songs->is_array()) {
        spdlog::error("Database {} has no 'songs' array.", path.string());
        return std::nullopt;
    }
    Database db;
    for (const auto &song : *songs) {
        const auto sha = song.find("sha256");
        if (sha == song.end() || !sha->is_string()) {
            continue;
        }
        DatabaseEntry entry;
        entry.id = song.value("id", std::string());
        entry.keyType = song.value("key_type", 0);
        entry.world = song.value("world", false);
        entry.bundled = song.value("bundled", false);
        if (const auto it = song.find("jpn_id"); it != song.end() && it->is_string()) {
            entry.jpnId = it->get<std::string>();
        }
        entry.isRevision = song.contains("revision_of");
        db.bySha.emplace(toLowerAscii(sha->get<std::string>()), std::move(entry));
    }
    spdlog::debug("Loaded {} database entries from {}.", db.bySha.size(), path.string());
    return db;
}

// Return the Japanese-release id a world entry maps to: the explicit jpn_id, or the entry id with
// its leading W removed when no jpn_id is recorded (the world copy shares the Japanese id).
std::string japaneseEquivalentId(const DatabaseEntry &entry) {
    if (entry.jpnId) {
        return *entry.jpnId;
    }
    if (entry.id.starts_with('W')) {
        return entry.id.substr(1);
    }
    return entry.id;
}

// Return the single passphrase the database indicates for a package: the lovers key for world
// packages, otherwise the regular or DLC key selected by key_type. Decrypting with this one key is
// sufficient, so trialing every candidate is unnecessary when the database matched the file.
std::string keyForEntry(const DatabaseEntry &entry) {
    if (entry.world) {
        return kReflecWorldKey;
    }
    return entry.keyType == 1 ? kReflecJpDlcKey : kReflecRegularKey;
}

// Create a codec from a passphrase (MD5-hashed to 16 bytes), or nullopt when the key cannot be
// derived.
std::optional<BFCodec> codecForPassphrase(const std::string &key) {
    const std::string_view passphrase(key);
    auto digest = Tools::md5Key(std::span<const std::byte>(
        reinterpret_cast<const std::byte *>(passphrase.data()), passphrase.size()));
    if (!digest) {
        return std::nullopt;
    }
    std::vector<std::byte> keyBytes(digest->begin(), digest->end());
    auto codec = Tools::createCodec(keyBytes);
    if (!codec) {
        return std::nullopt;
    }
    return std::move(*codec);
}

// Quote a single argument the way Python's shlex.quote does, so the rendered "Would run" lines and
// the rsync destination argument match the reference tool byte for byte.
std::string shellQuote(const std::string &s) {
    if (s.empty()) {
        return "''";
    }
    bool safe = true;
    for (const unsigned char c : s) {
        const bool word =
            (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
        const bool punctuation = c == '@' || c == '%' || c == '+' || c == '-' || c == '=' ||
                                 c == ':' || c == ',' || c == '.' || c == '/';
        if (!word && !punctuation) {
            safe = false;
            break;
        }
    }
    if (safe) {
        return s;
    }
    std::string out = "'";
    for (const char c : s) {
        if (c == '\'') {
            out += "'\"'\"'";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

// Render a command list as a single shell-quoted string for logging.
std::string shellJoin(const std::vector<std::string> &command) {
    std::string out;
    for (size_t i = 0; i < command.size(); ++i) {
        if (i != 0) {
            out += ' ';
        }
        out += shellQuote(command[i]);
    }
    return out;
}

// The connection parameters carried by the HOST positional. host is always set; the others are
// present only when the URI form supplied them.
struct HostInfo {
    std::string host;
    std::optional<std::string> user;
    std::optional<std::string> password;
    std::optional<int> port;
};

// Parse the HOST positional, which is either a bare hostname or IP address or a URI of the form
// [user[:password]@]host[:port]. The last '@' separates the user-info (so a password may contain
// '@'); the first ':' in the user-info separates the user from the password (so a password may
// contain ':'). A trailing ':port' is recognised only when every character after the last colon is
// a digit and the remaining host has no colon, which leaves bare IPv6 addresses untouched. Returns
// nullopt when the host is empty or the port is not a valid number.
std::optional<HostInfo> parseHost(const std::string &raw) {
    HostInfo info;
    std::string rest = raw;
    if (const auto at = rest.rfind('@'); at != std::string::npos) {
        const std::string userInfo = rest.substr(0, at);
        rest = rest.substr(at + 1);
        if (const auto colon = userInfo.find(':'); colon == std::string::npos) {
            info.user = userInfo;
        } else {
            info.user = userInfo.substr(0, colon);
            info.password = userInfo.substr(colon + 1);
        }
    }
    if (const auto colon = rest.rfind(':'); colon != std::string::npos && colon + 1 < rest.size()) {
        const std::string portText = rest.substr(colon + 1);
        const std::string hostText = rest.substr(0, colon);
        const bool allDigits = std::all_of(
            portText.begin(), portText.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
        if (allDigits && hostText.find(':') == std::string::npos) {
            try {
                info.port = std::stoi(portText);
            } catch (const std::exception &) {
                return std::nullopt;
            }
            rest = hostText;
        }
    }
    if (rest.empty()) {
        return std::nullopt;
    }
    info.host = rest;
    return info;
}

// Export the password to the SSHPASS environment variable so a child sshpass invocation can read it
// without it appearing on any command line. Returns false when the variable cannot be set.
bool setPasswordEnv(const std::string &password) {
#ifdef _WIN32
    return _putenv_s("SSHPASS", password.c_str()) == 0;
#else
    return ::setenv("SSHPASS", password.c_str(), 1) == 0;
#endif
}

// Prepend `sshpass -e` to a remote command when a password was supplied, so ssh and scp read it
// from the SSHPASS environment variable instead of prompting.
std::vector<std::string> withPassword(std::vector<std::string> command, bool usePassword) {
    if (usePassword) {
        command.insert(command.begin(), {std::string("sshpass"), std::string("-e")});
    }
    return command;
}

// A decrypted package: the mulist <dict> entry built from its info, and the passphrase that worked
// (so a world-version package can be recognised and re-encrypted).
struct ExtractedEntry {
    std::string dictEntry;
    std::string key;
};

// Try each known key for the package and return its mulist <dict> entry and the working key. Returns
// nullopt when no candidate key decrypts the package; the last diagnostic is surfaced as a warning.
std::optional<ExtractedEntry> extractEntry(const fs::path &package,
                                           const std::vector<std::string> &keys) {
    const auto raw = Tools::readZipEntry(package, "info");
    std::string lastError;
    for (const auto &key : keys) {
        spdlog::debug("Trying key '{}' for {}.", key, package.filename().string());
        if (!raw) {
            lastError = "Cannot read 'info' entry from archive.";
            continue;
        }
        auto codec = codecForPassphrase(key);
        if (!codec) {
            lastError = "Cannot derive key.";
            continue;
        }
        auto decrypted = Tools::decryptBfcEntry(*raw, *codec, Tools::kDefaultIv);
        if (!decrypted) {
            lastError = "Cannot decrypt 'info' entry (wrong key?).";
            continue;
        }
        std::string error;
        auto dictEntry = Tools::buildMulistEntry(*decrypted, package, error);
        if (!dictEntry) {
            lastError = error;
            continue;
        }
        spdlog::debug("Decrypted {} with key '{}'.", package.filename().string(), key);
        return ExtractedEntry{*dictEntry, key};
    }
    const std::string diagnostic =
        lastError.empty() ? std::string("no diagnostic output") : lastError;
    spdlog::warn("No known key worked for {} ({}).", package.filename().string(), diagnostic);
    return std::nullopt;
}

// Return the on-device filename: an optional leading W and the run of digits, joined with the
// lowercased extension. When the filename does not start with digits the original name is kept.
std::string trimmedName(const fs::path &package) {
    const std::string stem = package.stem().string();
    size_t i = 0;
    if (i < stem.size() && stem[i] == 'W') {
        ++i;
    }
    const size_t start = i;
    while (i < stem.size() && std::isdigit(static_cast<unsigned char>(stem[i]))) {
        ++i;
    }
    if (i == start) {
        spdlog::warn("Keeping {} as-is; it does not start with digits.",
                     package.filename().string());
        return package.filename().string();
    }
    return stem.substr(start, i - start) + toLowerAscii(package.extension().string());
}

// Wrap the collected <dict> entries in the property-list array envelope.
std::string buildMulist(const std::vector<std::string> &entries) {
    std::string body;
    for (const auto &entry : entries) {
        body += entry;
        body += '\n';
    }
    return kMulistHeader + body + kMulistFooter;
}

std::array<uint8_t, 4> randomPrefix() {
    std::random_device device;
    std::array<uint8_t, 4> out{};
    for (uint8_t &b : out) {
        b = static_cast<uint8_t>(device() & 0xFFu);
    }
    return out;
}

// Return a random hex string, used to name a unique temporary directory.
std::string randomHex() {
    std::random_device device;
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(16);
    for (int i = 0; i < 16; ++i) {
        out += kDigits[device() & 0xFu];
    }
    return out;
}

// Test whether a directory accepts new entries by creating and removing a probe subdirectory. This
// is more reliable than inspecting permission bits, which cannot account for ACLs or read-only
// mounts.
bool directoryIsWritable(const fs::path &dir) {
    std::error_code ec;
    const fs::path probe = dir / (".deploy-rb-probe-" + randomHex());
    if (!fs::create_directory(probe, ec) || ec) {
        return false;
    }
    fs::remove(probe, ec);
    return true;
}

// Resolve and create the local staging directory. An explicit --staging-dir override must already
// exist and be writable, otherwise the run fails immediately. Without an override a uniquely named,
// dot-prefixed directory is created in the current directory, falling back to the system temporary
// directory when the current directory is not writable. Returns nullopt on a fatal error, which has
// already been logged. During a dry run nothing is created and the would-be path is returned.
std::optional<fs::path> resolveStagingDir(const std::optional<std::string> &overridePath,
                                          bool dryRun) {
    std::error_code ec;
    if (overridePath) {
        const fs::path dir(*overridePath);
        if (!fs::exists(dir, ec)) {
            spdlog::error("Staging directory {} does not exist.", dir.string());
            return std::nullopt;
        }
        if (!fs::is_directory(dir, ec)) {
            spdlog::error("Staging path {} is not a directory.", dir.string());
            return std::nullopt;
        }
        // The writability probe writes a file, so it is skipped during a dry run.
        if (!dryRun && !directoryIsWritable(dir)) {
            spdlog::error("Staging directory {} is not writable.", dir.string());
            return std::nullopt;
        }
        spdlog::debug("Staging directory: {}.", dir.string());
        return dir;
    }
    const std::string name = ".deploy-rb-" + randomHex();
    if (dryRun) {
        const fs::path dir = fs::path(".") / name;
        spdlog::info("Would create staging directory: {}.", dir.string());
        return dir;
    }
    fs::path dir = fs::current_path(ec) / name;
    if (!ec) {
        fs::create_directory(dir, ec);
        if (!ec) {
            spdlog::debug("Staging directory: {}.", dir.string());
            return dir;
        }
    }
    spdlog::warn("Current directory is not writable; using the system temporary directory.");
    dir = fs::temp_directory_path(ec) / name;
    if (ec) {
        spdlog::error("Cannot locate a temporary directory.");
        return std::nullopt;
    }
    fs::create_directory(dir, ec);
    if (ec) {
        spdlog::error("Cannot create staging directory: {}.", dir.string());
        return std::nullopt;
    }
    spdlog::debug("Staging directory: {}.", dir.string());
    return dir;
}

// Run a command, or log it when dryRun is set. The named tool is resolved on PATH first so a missing
// ssh/scp/rsync produces a clear error on every platform rather than an opaque failure. The child
// inherits the terminal so ssh and scp can prompt for a password interactively. Returns false on a
// missing tool or non-zero exit.
bool runCommand(const std::vector<std::string> &command, bool dryRun, bool verbose) {
    const std::string rendered = shellJoin(command);
    if (dryRun) {
        spdlog::info("Would run: {}", rendered);
        return true;
    }
    auto resolved = Tools::findInPath(command.front());
    if (!resolved) {
        spdlog::error("Cannot find {} in PATH.", command.front());
        return false;
    }
    if (verbose) {
        spdlog::info("Running: {}", rendered);
    } else {
        spdlog::debug("Running: {}", rendered);
    }
    std::vector<std::string> argv = command;
    argv.front() = *resolved;
    const int code = Tools::runProcess(argv);
    if (code != 0) {
        spdlog::error("{} exited with status {}.", command.front(), code);
        return false;
    }
    return true;
}

// Return the leading ssh/scp arguments shared by every remote call. portOption is the port flag,
// which differs between the tools (-p for ssh, -P for scp).
std::vector<std::string> sshPrefix(const std::string &portOption,
                                   int port,
                                   const std::optional<std::string> &identity,
                                   const std::vector<std::string> &authOptions) {
    std::vector<std::string> prefix;
    prefix.push_back(portOption);
    prefix.push_back(std::to_string(port));
    prefix.insert(prefix.end(), authOptions.begin(), authOptions.end());
    if (identity) {
        prefix.push_back("-i");
        prefix.push_back(*identity);
    }
    return prefix;
}

// Return the value for rsync's -e option: the ssh command with key-only auth options.
std::string rsyncRemoteShell(int port, const std::optional<std::string> &identity) {
    std::vector<std::string> shell = {"ssh", "-p", std::to_string(port)};
    shell.insert(shell.end(), kKeyOnlySshOptions.begin(), kKeyOnlySshOptions.end());
    if (identity) {
        shell.push_back("-i");
        shell.push_back(*identity);
    }
    return shellJoin(shell);
}

// Return true when the device has rsync. The probe runs command -v rsync over SSH; only its exit
// status matters, since rsync's own ssh resolves rsync against the same remote PATH. During a dry run
// no connection is made and false is returned, so the simulated commands fall back to scp.
bool remoteHasRsync(const std::string &target,
                    int port,
                    const std::optional<std::string> &identity,
                    const std::vector<std::string> &authOptions,
                    bool usePassword,
                    bool dryRun,
                    bool verbose) {
    std::vector<std::string> command = {"ssh"};
    const std::vector<std::string> prefix = sshPrefix("-p", port, identity, authOptions);
    command.insert(command.end(), prefix.begin(), prefix.end());
    command.push_back(target);
    command.push_back("command");
    command.push_back("-v");
    command.push_back("rsync");
    command = withPassword(command, usePassword);
    const std::string rendered = shellJoin(command);
    if (dryRun) {
        spdlog::info("Would check for rsync on the device: {}", rendered);
        return false;
    }
    auto resolved = Tools::findInPath(command.front());
    if (!resolved) {
        spdlog::error("Cannot find {} in PATH.", command.front());
        return false;
    }
    if (verbose) {
        spdlog::info("Checking for rsync on the device: {}", rendered);
    } else {
        spdlog::debug("Checking for rsync on the device: {}", rendered);
    }
    std::vector<std::string> argv = command;
    argv.front() = *resolved;
    const bool available = Tools::runProcess(argv) == 0;
    spdlog::debug(available ? "rsync is available on the device." :
                              "rsync is not available on the device.");
    return available;
}

// Copy sources into remoteDir on the device. rsync is used only when useRsyncTransport is set;
// otherwise scp is used.
bool copyToRemote(const std::vector<fs::path> &sources,
                  const std::string &remoteDir,
                  const std::string &target,
                  int port,
                  const std::optional<std::string> &identity,
                  const std::vector<std::string> &authOptions,
                  bool useRsyncTransport,
                  bool usePassword,
                  bool dryRun,
                  bool verbose) {
    std::vector<std::string> command;
    if (useRsyncTransport) {
        // rsync passes the destination through the remote shell, so the path (which may contain a
        // space) is quoted to survive that extra round of word splitting. rsync always uses
        // key-only authentication, so no password is applied here.
        command.push_back("rsync");
        command.push_back("-e");
        command.push_back(rsyncRemoteShell(port, identity));
        for (const auto &source : sources) {
            command.push_back(source.string());
        }
        command.push_back(target + ":" + shellQuote(remoteDir + "/"));
    } else {
        command.push_back("scp");
        const std::vector<std::string> prefix = sshPrefix("-P", port, identity, authOptions);
        command.insert(command.end(), prefix.begin(), prefix.end());
        for (const auto &source : sources) {
            command.push_back(source.string());
        }
        command.push_back(target + ":" + remoteDir + "/");
        command = withPassword(command, usePassword);
    }
    return runCommand(command, dryRun, verbose);
}

// Create the remote directories and copy the mulist and song packages into the container.
bool deploy(const fs::path &mulistPath,
            const std::vector<fs::path> &songPaths,
            const std::string &user,
            const std::string &host,
            const std::string &appUuid,
            int port,
            const std::optional<std::string> &identity,
            bool useRsync,
            bool usePassword,
            bool dryRun,
            bool verbose) {
    const std::string target = user + "@" + host;
    const std::string container = kRemoteContainerRoot + "/" + appUuid;
    const std::string documents = container + "/Documents";
    const std::string privateDocuments = container + "/Library/Private Documents";
    // Opting into rsync also commits to public-key authentication, since rsync's own ssh cannot
    // answer a password prompt.
    const std::vector<std::string> &authOptions = useRsync ? kKeyOnlySshOptions : kSshAuthOptions;

    // A single mkdir -p ensures both destinations exist. The paths are passed as separate arguments
    // because the remote shell, not this process, splits the command.
    std::vector<std::string> mkdir = {"ssh"};
    const std::vector<std::string> prefix = sshPrefix("-p", port, identity, authOptions);
    mkdir.insert(mkdir.end(), prefix.begin(), prefix.end());
    mkdir.push_back(target);
    mkdir.push_back("mkdir");
    mkdir.push_back("-p");
    mkdir.push_back(documents);
    mkdir.push_back(privateDocuments);
    if (!runCommand(withPassword(mkdir, usePassword), dryRun, verbose)) {
        return false;
    }

    // The mulist is always copied with scp; only the larger song packages may use rsync.
    if (!copyToRemote({mulistPath},
                      documents,
                      target,
                      port,
                      identity,
                      authOptions,
                      false,
                      usePassword,
                      dryRun,
                      verbose)) {
        return false;
    }

    if (!songPaths.empty()) {
        bool useRsyncTransport = false;
        if (useRsync) {
            useRsyncTransport =
                remoteHasRsync(target, port, identity, authOptions, usePassword, dryRun, verbose);
            if (!useRsyncTransport && !dryRun) {
                spdlog::warn("rsync requested but not available on the device; using scp.");
            }
        }
        if (!copyToRemote(songPaths,
                          privateDocuments,
                          target,
                          port,
                          identity,
                          authOptions,
                          useRsyncTransport,
                          usePassword,
                          dryRun,
                          verbose)) {
            return false;
        }
    }
    return true;
}

// Encrypt the plaintext mulist with the key derived from the device UUID and the fixed IV, writing
// the same on-disk trailer layout bfc writes. Returns false on any failure.
bool encryptMulist(const std::string &uuid,
                   const std::vector<uint8_t> &plain,
                   const fs::path &outPath,
                   bool verbose) {
    auto canonical = Tools::canonicalizeUuid(uuid);
    if (!canonical) {
        spdlog::error("{}", Tools::message(canonical.error()));
        return false;
    }
    const std::string_view canonicalView(*canonical);
    auto digest = Tools::md5Key(std::span<const std::byte>(
        reinterpret_cast<const std::byte *>(canonicalView.data()), canonicalView.size()));
    if (!digest) {
        spdlog::error("{}", Tools::message(digest.error()));
        return false;
    }
    std::vector<std::byte> keyBytes(digest->begin(), digest->end());
    auto codec = Tools::createCodec(keyBytes);
    if (!codec) {
        spdlog::error("{}", BFCodec::message(codec.error()));
        return false;
    }

    const uint32_t fileSize = static_cast<uint32_t>(plain.size());
    std::vector<uint8_t> buf = plain;
    const size_t paddedSize = (buf.size() + 7u) / 8u * 8u;
    buf.resize(paddedSize, 0);
    const uint32_t blockSize = static_cast<uint32_t>(buf.size());
    if (blockSize == 0 || (buf.size() % 8u) != 0u) {
        spdlog::error("Invalid block size for the mulist.");
        return false;
    }

    std::span<std::byte> span = std::as_writable_bytes(std::span(buf));
    auto result = codec->encrypt(span, Tools::kDefaultIv);
    if (!result) {
        spdlog::error("{}", BFCodec::message(result.error()));
        return false;
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

    auto write = Tools::writeWholeFile(outPath, std::span<const uint8_t>(out.data(), out.size()));
    if (!write) {
        spdlog::error("{}", Tools::message(write.error()));
        return false;
    }
    if (verbose) {
        spdlog::info("Built encrypted mulist at {}.", outPath.string());
    } else {
        spdlog::debug("Built encrypted mulist at {}.", outPath.string());
    }
    return true;
}

// Convert a world-version (jp.konami.rbplusw) package to the Japanese release format: unpack it to a
// temporary directory with the world key, then re-pack it with the Japanese DLC key the supported
// app reads. Returns false on failure.
bool convertWorldPackage(const fs::path &source, const fs::path &destination) {
    auto worldCodec = codecForPassphrase(kReflecWorldKey);
    auto japaneseCodec = codecForPassphrase(kReflecJpDlcKey);
    if (!worldCodec || !japaneseCodec) {
        spdlog::error("Cannot derive keys for world-version conversion.");
        return false;
    }
    std::error_code ec;
    const fs::path tempDir = fs::temp_directory_path(ec) / ("deploy-rb-" + randomHex());
    if (ec) {
        spdlog::error("Cannot locate a temporary directory.");
        return false;
    }
    fs::create_directories(tempDir, ec);
    if (ec) {
        spdlog::error("Cannot create temporary directory: {}.", tempDir.string());
        return false;
    }
    const bool ok =
        Tools::unpackArchive(source, tempDir, *worldCodec, Tools::kDefaultIv) &&
        Tools::packArchive(tempDir, destination, *japaneseCodec, Tools::kDefaultIv).has_value();
    fs::remove_all(tempDir, ec);
    return ok;
}

// Run fn(i) for every i in [0, count) across at most jobs worker threads. Each index is handled by
// exactly one thread, so fn is safe as long as it only touches data unique to its index.
template <typename Fn>
void parallelFor(size_t count, unsigned jobs, Fn fn) {
    if (count == 0) {
        return;
    }
    const unsigned workers =
        std::max<unsigned>(1u, std::min<unsigned>(jobs, static_cast<unsigned>(count)));
    if (workers == 1) {
        for (size_t i = 0; i < count; ++i) {
            fn(i);
        }
        return;
    }
    std::atomic<size_t> next{0};
    std::vector<std::thread> threads;
    threads.reserve(workers);
    for (unsigned w = 0; w < workers; ++w) {
        threads.emplace_back([&] {
            for (;;) {
                const size_t i = next.fetch_add(1);
                if (i >= count) {
                    break;
                }
                fn(i);
            }
        });
    }
    for (std::thread &thread : threads) {
        thread.join();
    }
}

} // namespace

int main(int argc, char *argv[]) {
    unsigned hardwareJobs = std::thread::hardware_concurrency();
    if (hardwareJobs == 0) {
        hardwareJobs = 1;
    }

    // When invoked as deploy-jbt (an installed symlink, or a copy on Windows), the package type
    // defaults to jbt instead of rb. The stem is compared so a .exe suffix is ignored.
    const std::string invokedAs =
        (argc > 0 && argv[0] != nullptr) ? fs::path(argv[0]).stem().string() : std::string();
    const std::string defaultType = invokedAs.ends_with("deploy-jbt") ? "jbt" : "rb";

    argparse::ArgumentParser program("deploy-rb");
    program.add_description(
        "Build a complete mulist from song packages and deploy it to an iOS device over SSH.\n\n"
        "Note: the Rb+ world version (jp.konami.rbplusw) is not supported as a target. Only the "
        "Japanese release (jp.konami.rbplus) is supported; world-version packages are skipped "
        "unless --convert-world is given, which re-encrypts them to the Japanese format.");

    program.add_argument("HOST").help(
        "SSH host of the device. May be a hostname or IP address, or a URI of the form "
        "[user[:password]@]host[:port] to also set the user, password, and port.");
    program.add_argument("APP-UUID")
        .help("Application container UUID naming the remote directory.");
    program.add_argument("UUID").help(
        "Device Keychain UUID; the mulist key is MD5 of its uppercase form.");
    program.add_argument("FILES")
        .help("The .rb / .jbt packages to include.")
        .nargs(argparse::nargs_pattern::at_least_one);
    program.add_argument("-W", "--convert-world")
        .help("Re-encrypt world-version (jp.konami.rbplusw) packages for the Japanese release "
              "instead of skipping them.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("--db")
        .default_value(std::string("goodrb.json"))
        .help("Path to the goodrb.json database used to validate rb packages by SHA-256 "
              "(default: goodrb.json in the current directory).");
    program.add_argument("-d", "--debug")
        .help("Enable debug-level logging.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-y", "--dry-run")
        .help("Simulate every step, creating no files and only logging commands.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-i", "--identity").help("SSH identity (private key) file.");
    program.add_argument("-j", "--jobs")
        .scan<'i', int>()
        .default_value(static_cast<int>(hardwareJobs))
        .help("Number of files to process in parallel (default: number of CPUs).");
    program.add_argument("-K", "--keep")
        .help("Keep the staging directory after deployment instead of removing it.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-p", "--password")
        .help("SSH password, fed to ssh and scp through sshpass. Overridden by a password given in "
              "HOST; incompatible with --rsync.");
    program.add_argument("-P", "--port")
        .scan<'i', int>()
        .default_value(22)
        .help("SSH port (default: 22). Overridden by a port given in HOST.");
    program.add_argument("-B", "--process-bundled")
        .help("Include packages the database marks as bundled, which are skipped by default.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-N", "--process-non-matching")
        .help("Process every input file without consulting the database. This disables the SHA-256 "
              "check.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-r", "--rsync")
        .help("Use rsync for the song packages when the device has it. Requires public-key "
              "authentication (no password prompt); the mulist itself is always copied with scp.")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-S", "--staging-dir")
        .help("Local directory for the built mulist and renamed packages. When given it must "
              "already exist and be writable. Otherwise a uniquely named '.deploy-rb-*' directory "
              "is created in the current directory, or in the system temporary directory when the "
              "current directory is not writable.");
    program.add_argument("-t", "--type")
        .default_value(defaultType)
        .choices("rb", "jbt")
        .help("Package type selecting the decryption keys: rb or jbt. Defaults to jbt when invoked "
              "as deploy-jbt, otherwise rb.");
    program.add_argument("-u", "--user")
        .default_value(std::string("mobile"))
        .help("SSH user (default: mobile). Overridden by a user given in HOST.");
    program.add_argument("-v", "--verbose")
        .help("Report each step.")
        .default_value(false)
        .implicit_value(true);

    spdlog::set_default_logger(spdlog::stderr_color_mt("deploy-rb"));
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

    const bool dryRun = program.get<bool>("--dry-run");
    const bool verbose = program.get<bool>("--verbose");
    const bool debug = program.get<bool>("--debug");
    if (debug) {
        spdlog::set_level(spdlog::level::debug);
        spdlog::set_pattern("%l: %v");
    }

    const std::string uuid = program.get<std::string>("UUID");
    // Validate both UUIDs up front so a malformed value fails before any work, including in a dry
    // run where the device UUID would otherwise only be checked when the key is derived. The device
    // UUID derives the mulist key; the application UUID names the remote container directory, which
    // iOS stores in canonical uppercase form, so the canonicalised value is used there.
    if (const auto canonical = Tools::canonicalizeUuid(uuid); !canonical) {
        spdlog::error("UUID is not a valid UUID: {}", Tools::message(canonical.error()));
        return EXIT_FAILURE;
    }
    const auto canonicalAppUuid = Tools::canonicalizeUuid(program.get<std::string>("APP-UUID"));
    if (!canonicalAppUuid) {
        spdlog::error("APP-UUID is not a valid UUID: {}", Tools::message(canonicalAppUuid.error()));
        return EXIT_FAILURE;
    }
    const std::string appUuid = *canonicalAppUuid;
    // HOST is a free-form string: a bare hostname or IP address, or a [user[:password]@]host[:port]
    // URI. Values parsed from the URI take precedence over the --user and --port options.
    const auto hostInfo = parseHost(program.get<std::string>("HOST"));
    if (!hostInfo) {
        spdlog::error("HOST is not a valid host or URI.");
        return EXIT_FAILURE;
    }
    const std::string host = hostInfo->host;
    const std::string user = hostInfo->user.value_or(program.get<std::string>("--user"));
    const int port = hostInfo->port.value_or(program.get<int>("--port"));
    // A password in HOST takes precedence over the --password option.
    const std::optional<std::string> password =
        hostInfo->password ? hostInfo->password : program.present<std::string>("--password");
    const std::optional<std::string> identity = program.present<std::string>("--identity");
    const std::string type = program.get<std::string>("--type");
    const bool convertWorld = program.get<bool>("--convert-world");
    const bool processNonMatching = program.get<bool>("--process-non-matching");
    const bool processBundled = program.get<bool>("--process-bundled");
    const fs::path databasePath = program.get<std::string>("--db");
    const bool useRsync = program.get<bool>("--rsync");
    const bool keep = program.get<bool>("--keep");
    const auto stagingArg = program.present<std::string>("--staging-dir");
    const auto packages = program.get<std::vector<std::string>>("FILES");
    const std::vector<std::string> keys = candidateKeys(type);

    // A password is fed to ssh and scp through sshpass. That is incompatible with rsync, which uses
    // key-only authentication, and requires sshpass to be installed and the SSHPASS variable set.
    const bool usePassword = password.has_value();
    if (usePassword) {
        if (useRsync) {
            spdlog::error("A password cannot be used with --rsync, which requires public-key "
                          "authentication.");
            return EXIT_FAILURE;
        }
        if (!Tools::findInPath("sshpass")) {
            spdlog::error("A password was given but sshpass was not found in PATH.");
            return EXIT_FAILURE;
        }
        if (!dryRun && !setPasswordEnv(*password)) {
            spdlog::error("Cannot set the SSHPASS environment variable.");
            return EXIT_FAILURE;
        }
    }

    // The goodrb.json database is the authority for rb packages. --process-non-matching restores
    // the original behaviour of trusting every input file, so the database is not loaded then. jbt
    // packages are never validated against it.
    const bool useDatabase = type == "rb" && !processNonMatching;
    std::optional<Database> database;
    if (useDatabase) {
        database = loadDatabase(databasePath);
        if (!database) {
            spdlog::error("Pass --db with a valid goodrb.json, or --process-non-matching to "
                          "process files without it.");
            return EXIT_FAILURE;
        }
    }

    const int jobsArg = program.get<int>("--jobs");
    if (jobsArg < 1) {
        spdlog::error("--jobs must be at least 1.");
        return EXIT_FAILURE;
    }
    const unsigned jobs = static_cast<unsigned>(jobsArg);

    const auto resolvedStaging = resolveStagingDir(stagingArg, dryRun);
    if (!resolvedStaging) {
        return EXIT_FAILURE;
    }
    const fs::path staging = *resolvedStaging;

    // A successfully decrypted package: its mulist <dict> entry, original path, whether it is a
    // world-version package needing conversion, and the matched database entry when the database is
    // the authority.
    struct Parsed {
        fs::path package;
        std::string dictEntry;
        bool worldVersion;
        std::optional<DatabaseEntry> dbEntry;
    };

    // Phase A: extract and decrypt every package in parallel. The entry is read from the original
    // filename so its ItemURL keeps the download token. This work is independent per file; each task
    // writes only its own slot, so no synchronisation is needed.
    std::vector<std::optional<Parsed>> parsed(packages.size());
    parallelFor(packages.size(), jobs, [&](size_t i) {
        const fs::path package(packages[i]);
        if (!fs::is_regular_file(package)) {
            spdlog::warn("Package {} is not a file; skipping.", package.string());
            return;
        }
        spdlog::debug("Processing {}.", package.string());
        if (useDatabase) {
            // The database is the authority: match the file by SHA-256, which also tells us the one
            // key to use. An unmatched file is dropped rather than deployed, and a matched file that
            // does not decrypt with its indicated key is a bad file. The matched entry's world and
            // bundled flags drive the later filtering, regardless of the filename.
            auto bytes = Tools::readWholeFile(package);
            if (!bytes) {
                spdlog::warn("Cannot read {} to check its SHA-256; skipping.", package.string());
                return;
            }
            auto sha = Tools::sha256Hex(std::as_bytes(std::span(*bytes)));
            if (!sha) {
                spdlog::warn("Cannot hash {}; skipping.", package.string());
                return;
            }
            const auto it = database->bySha.find(*sha);
            if (it == database->bySha.end()) {
                spdlog::warn("{} matches no database entry (SHA-256 {}); skipping. Pass "
                             "--process-non-matching to process it anyway.",
                             package.filename().string(),
                             *sha);
                return;
            }
            const DatabaseEntry &entry = it->second;
            const std::string key = keyForEntry(entry);
            auto extracted = extractEntry(package, {key});
            if (!extracted) {
                spdlog::warn("{} did not decrypt with its database key '{}'; bad file. Skipping.",
                             package.filename().string(),
                             key);
                return;
            }
            parsed[i] = Parsed{package, extracted->dictEntry, entry.world, entry};
        } else {
            // Without the database, every candidate key is trialed as before.
            auto extracted = extractEntry(package, keys);
            if (!extracted) {
                return;
            }
            parsed[i] = Parsed{
                package, extracted->dictEntry, extracted->key == kReflecWorldKey, std::nullopt};
        }
    });

    // The Japanese-release ids present in the matched input, so a world package can be dropped when
    // its Japanese equivalent is also being processed.
    std::set<std::string> presentJapaneseIds;
    if (useDatabase) {
        for (const auto &result : parsed) {
            if (result && result->dbEntry && !result->dbEntry->world) {
                presentJapaneseIds.insert(result->dbEntry->id);
            }
        }
    }

    // Phase B: resolve world-version handling and duplicates in input order, so the result is
    // deterministic regardless of how the parallel extraction interleaved.
    std::vector<std::string> entries;
    std::vector<fs::path> songPaths;
    std::vector<Parsed> toStage;
    size_t skipped = 0;
    std::unordered_map<std::string, size_t> stagedIndexByName;
    for (const auto &result : parsed) {
        if (!result) {
            // Either not a file or no key worked; both were already logged in phase A.
            ++skipped;
            continue;
        }
        // Bundled songs ship inside the app, so they are skipped unless explicitly requested.
        if (result->dbEntry && result->dbEntry->bundled && !processBundled) {
            spdlog::warn(
                "Skipping bundled package {} (id {}); pass --process-bundled to include it.",
                result->package.filename().string(),
                result->dbEntry->id);
            ++skipped;
            continue;
        }
        // A world package whose Japanese equivalent is also present is a duplicate; the Japanese
        // file is always preferred, so the world copy is dropped before any conversion.
        if (result->worldVersion && result->dbEntry) {
            const std::string equivalent = japaneseEquivalentId(*result->dbEntry);
            if (presentJapaneseIds.count(equivalent) != 0) {
                spdlog::warn("Skipping world package {}; its Japanese equivalent (id {}) is also "
                             "being processed.",
                             result->package.filename().string(),
                             equivalent);
                ++skipped;
                continue;
            }
        }
        // A world-version package can only be deployed by re-encrypting it to the Japanese release
        // format, which requires --convert-world; without that flag it is skipped rather than
        // deployed in a form the supported app cannot read. Any other package is already readable by
        // the supported app and is copied verbatim.
        if (result->worldVersion && !convertWorld) {
            spdlog::warn("Skipping world-version package {}; pass --convert-world to re-encrypt it "
                         "for the Japanese release.",
                         result->package.filename().string());
            ++skipped;
            continue;
        }
        // With a database match the authoritative id names the on-device file; a world package is
        // converted to the Japanese release, so its Japanese-equivalent id is used. Without a
        // database entry (jbt packages, or --process-non-matching) the name is still derived from
        // the input filename.
        const std::string name = result->dbEntry ?
                                     japaneseEquivalentId(*result->dbEntry) +
                                         toLowerAscii(result->package.extension().string()) :
                                     trimmedName(result->package);
        const bool isRevision = result->dbEntry && result->dbEntry->isRevision;
        Parsed staged{result->package, result->dictEntry, result->worldVersion, result->dbEntry};
        const auto existing = stagedIndexByName.find(name);
        if (existing != stagedIndexByName.end()) {
            const size_t idx = existing->second;
            const bool existingIsRevision =
                toStage[idx].dbEntry && toStage[idx].dbEntry->isRevision;
            // A revision wins over its original; otherwise the first staged file is kept.
            if (isRevision && !existingIsRevision) {
                spdlog::warn("Package {} is a revision of {}; preferring it over the earlier copy.",
                             result->package.filename().string(),
                             name);
                toStage[idx] = std::move(staged);
                songPaths[idx] = staging / name;
                entries[idx] = result->dictEntry;
            } else {
                spdlog::warn("Package {} trims to {}, already staged; skipping the duplicate.",
                             result->package.filename().string(),
                             name);
            }
            ++skipped;
            continue;
        }
        stagedIndexByName.emplace(name, toStage.size());
        // toStage keeps the source path and conversion flag; songPaths holds the matching
        // destination, so the two vectors stay index-aligned for phase C.
        toStage.push_back(std::move(staged));
        songPaths.push_back(staging / name);
        entries.push_back(result->dictEntry);
    }

    if (entries.empty()) {
        spdlog::warn("No packages could be decrypted; refusing to build an empty mulist.");
        return EXIT_FAILURE;
    }

    // Phase C: copy or convert each surviving package into the staging directory in parallel. Each
    // task writes a distinct destination, so the only shared state is the failure flag.
    if (dryRun) {
        for (size_t i = 0; i < toStage.size(); ++i) {
            if (toStage[i].worldVersion) {
                spdlog::info("Would convert world-version {} to {}.",
                             toStage[i].package.string(),
                             songPaths[i].string());
            } else {
                spdlog::info(
                    "Would stage {} as {}.", toStage[i].package.string(), songPaths[i].string());
            }
        }
    } else {
        std::atomic<bool> stageFailed{false};
        parallelFor(toStage.size(), jobs, [&](size_t i) {
            const Parsed &item = toStage[i];
            const fs::path &destination = songPaths[i];
            if (item.worldVersion) {
                if (!convertWorldPackage(item.package, destination)) {
                    spdlog::error("Cannot convert world-version package {}.",
                                  item.package.string());
                    stageFailed = true;
                    return;
                }
                spdlog::debug("Converted world-version {} to {}.",
                              item.package.string(),
                              destination.string());
            } else {
                std::error_code ec;
                fs::copy_file(item.package, destination, fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    spdlog::error(
                        "Cannot stage {} as {}.", item.package.string(), destination.string());
                    stageFailed = true;
                    return;
                }
                spdlog::debug("Staged {} as {}.", item.package.string(), destination.string());
            }
        });
        if (stageFailed) {
            return EXIT_FAILURE;
        }
    }

    const fs::path plaintext = staging / "mulist.plist";
    const fs::path encrypted = staging / "mulist";
    if (dryRun) {
        spdlog::info("Would write plaintext mulist with {} entries to {}.",
                     entries.size(),
                     plaintext.string());
        spdlog::info("Would build encrypted mulist at {}.", encrypted.string());
    } else {
        const std::string xml = buildMulist(entries);
        std::vector<uint8_t> plain;
        plain.reserve(4u + xml.size());
        const std::array<uint8_t, 4> prefix = randomPrefix();
        plain.insert(plain.end(), prefix.begin(), prefix.end());
        plain.insert(plain.end(), xml.begin(), xml.end());
        auto write =
            Tools::writeWholeFile(plaintext, std::span<const uint8_t>(plain.data(), plain.size()));
        if (!write) {
            spdlog::error("{}", Tools::message(write.error()));
            return EXIT_FAILURE;
        }
        spdlog::debug(
            "Wrote plaintext mulist with {} entries to {}.", entries.size(), plaintext.string());
        if (!encryptMulist(uuid, plain, encrypted, verbose)) {
            return EXIT_FAILURE;
        }
    }

    if (!deploy(encrypted,
                songPaths,
                user,
                host,
                appUuid,
                port,
                identity,
                useRsync,
                usePassword,
                dryRun,
                verbose)) {
        return EXIT_FAILURE;
    }

    const char *entryWord = entries.size() == 1 ? "entry" : "entries";
    const char *packageWord = songPaths.size() == 1 ? "package" : "packages";
    if (dryRun) {
        spdlog::info("Dry run: would build {} with {} {} and deploy {} {}.",
                     encrypted.string(),
                     entries.size(),
                     entryWord,
                     songPaths.size(),
                     packageWord);
    } else {
        spdlog::info("Built {} with {} {}; deployed {} {}.",
                     encrypted.string(),
                     entries.size(),
                     entryWord,
                     songPaths.size(),
                     packageWord);
    }

    // Report catalogue coverage. A song is counted once regardless of revisions (same id) or a
    // world copy (folded onto its Japanese equivalent), and bundled songs are excluded since they
    // ship inside the app and cannot be supplied as downloadable packages.
    if (useDatabase && database) {
        const auto songKey = [](const DatabaseEntry &entry) {
            return entry.world ? japaneseEquivalentId(entry) : entry.id;
        };
        std::set<std::string> knownSongs;
        for (const auto &[sha, entry] : database->bySha) {
            if (!entry.bundled) {
                knownSongs.insert(songKey(entry));
            }
        }
        std::set<std::string> haveSongs;
        for (const auto &result : parsed) {
            if (result && result->dbEntry && !result->dbEntry->bundled) {
                haveSongs.insert(songKey(*result->dbEntry));
            }
        }
        const size_t known = knownSongs.size();
        const size_t have = haveSongs.size();
        const size_t missing = known > have ? known - have : 0;
        spdlog::info("You are missing {} out of {} known songs.", missing, known);
    }

    // The staging directory holds the renamed packages and the built mulist; it is removed once
    // deployment is complete unless --keep is given.
    if (keep) {
        spdlog::debug("Keeping staging directory: {}.", staging.string());
    } else if (dryRun) {
        spdlog::info("Would remove staging directory: {}.", staging.string());
    } else {
        std::error_code ec;
        fs::remove_all(staging, ec);
        if (ec) {
            spdlog::warn("Cannot remove staging directory {}: {}.", staging.string(), ec.message());
        } else {
            spdlog::debug("Removed staging directory: {}.", staging.string());
        }
    }

    if (skipped != 0) {
        spdlog::warn("Skipped {} {}.", skipped, skipped == 1 ? "package" : "packages");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
