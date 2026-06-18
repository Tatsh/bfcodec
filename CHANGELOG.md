<!-- markdownlint-configure-file {"MD024": { "siblings_only": true } } -->

# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- New `deploy-rb` tool that builds a complete REFLEC BEAT plus `mulist` from song packages and
  deploys it, with the packages, to a jailbroken iOS device over SSH. It validates `.rb` packages
  against a `goodrb.json` database by SHA-256, optionally re-encrypts world-version packages for the
  Japanese release (`--convert-world`), and copies with `scp` or `rsync` (`--rsync`). The host may
  be given as a `[user[:password]@]host[:port]` URI, with passwords passed through `sshpass`.
  Building the tools now also requires nlohmann-json. A `deploy-rb.1` man page is installed. The
  tool is also installed as `deploy-jbt`, which defaults `--type` to `jbt`.
- `bfc` and `unbfc` accept a `--uuid` option that derives the key from a device UUID. The supplied
  UUID is normalised to its canonical uppercase `8-4-4-4-12` form before hashing, so dashes and
  letter case are optional and the result matches `MD5(CFUUIDCreateString(...))`. This decrypts
  files some iOS games key to a per-device UUID (for example `mulist` and `prodlist`).
- `bfc` and `unbfc` accept a `--backup` option that backs up the destination to `<dest>.bak`
  before overwriting it (when the destination already exists).
- `bfc` and `unbfc` accept a `-o`/`--output` option that writes the result to a given path
  instead of overwriting the input.

## [0.0.6] - 2026-05-10

### Changed

- Flatpak (`sh.tat.bfcodec.yml`) and Snap (`snapcraft.yaml`) manifests are now pinned to the
  released git tag and tracked in `cz` version files, so each release builds reproducibly from
  the matching upstream tag.
- Bumped the vcpkg baseline to `2026.04.27`.
- Man pages (`jbt.1`, `unjbt.1`) now record the project version in `.Os` and are tracked in `cz`
  version files.

### Fixed

- MSVC release archives no longer have an empty build-type segment in their file names (e.g.
  `bfcodec-{version}-msvc-{x64|arm64}-{Release|Debug}` instead of
  `bfcodec-{version}-msvc-{x64|arm64}-`). Multi-config generators (Visual Studio) now defer the
  build-type portion of `CPACK_PACKAGE_FILE_NAME` to cpack-run time via `CPACK_BUILD_CONFIG`.

## [0.0.5] - 2026-05-05

### Added

- AppImage release artefact for `jbt` on Linux x86_64 and aarch64.
- Snap package providing `jbt` (`bfcodec` snap, amd64 and arm64).
- Flatpak bundle (`sh.tat.bfcodec`) running `jbt`, built on the Freedesktop 24.08 runtime, for
  x86_64 and aarch64.
- MSVC ARM64 build, published alongside the existing MSVC x64 build on tag pushes.
- MSYS2 `clangarm64` build, published on tag pushes.
- ARM64 (`aarch64`) Linux build, published alongside the existing x86_64 Linux build.
- Automated downstream packaging on tag pushes: pull request to `gentoo/gentoo` for
  `dev-libs/bfcodec`, pull request to `msys2/MINGW-packages` for `mingw-w64-bfcodec`, and a WinGet
  manifest update for `Tatsh.bfcodec`.

### Changed

- Windows MSVC release archives are now named `bfcodec-{version}-msvc-{x64|arm64}-{BuildType}`
  (previously `msvc64`).
- CPack package file names include the architecture so release artefacts no longer collide across
  the build matrix (`Linux-x86_64`, `Linux-aarch64`, `Darwin-{arm64|x86_64}`, `mingw64`, `ucrt64`,
  `clangarm64`, `msvc-{x64|arm64}`).
- Lowered the minimum required CMake version from 3.31 to 3.28.

## [0.0.4] - 2026-03-08

### Added

- Visual C++ (MSVC) build support with vcpkg for dependencies (argparse, libplist, libzip, spdlog).
- CI job `build-msvc` on Windows: vcpkg manifest mode, Debug/Release matrix, NSIS + ZIP packaging.
- CMake option `FORCE_PKGCONFIG_LIBPLIST` to force PkgConfig for libplist instead of CMake
  config/vcpkg.
- Byte-swap helpers use compiler intrinsics (`_byteswap_ulong` / `__builtin_bswap32`) with `memcpy`
  for endian handling in `bfcodec.c`.

### Changed

- C standard set to C17 for all compilers (including MSVC); removed MSVC-only C standard
  workarounds.
- Windows CPack package name: `bfcodec-{version}-{msvc|ucrt|mingw}{32|64}-{BuildType}` (e.g.
  `bfcodec-0.0.3-msvc64-Release`).
- libplist discovery in tools: try `find_package(libplist)` then `find_package(unofficial-libplist)`
  before PkgConfig.

### Fixed

- macOS CPack: use `CMAKE_APPLE_SILICON_PROCESSOR` and `CMAKE_OSX_ARCHITECTURES` so Darwin-arm64
  is used on Apple Silicon instead of Darwin-x86_64.
- MSVC C17: use array parameters `p[4]` / `b[4]` instead of `[static 4]` (unsupported in MSVC C17).
- unjbt freshen/update: file time comparison uses `clock_cast` to system_clock on MSVC and
  `file_clock::from_sys` on macOS/GCC/Clang (MSVC does not provide `file_clock::from_sys`).
- CI build-msvc: install NSIS before Configure; read project version from `CMakeLists.txt` for
  CPack package file name.

## [0.0.3] - 2026-03-08

### Changed

- Code and build fixes for Clang `-Weverything` (CMake, library, tools).
- README fix.

### Fixed

- Compiler warnings and compatibility in `bfcodec.c`, `bfcodec.cc`, `jbt`, `jbtcommon`, and `unjbt`.

## [0.0.2] - 2026-03-07

### Added

- Symbol visibility: minimise exported symbols in the shared library (DSO); only public API is
  exported (GNU/Clang `-fvisibility=hidden`, MSVC `__declspec(dllexport/dllimport)` in headers).

## [0.0.1] - 2026-01-01

### Added

- First version.

[Unreleased]: https://github.com/Tatsh/bfcodec/compare/v0.0.5...HEAD
[0.0.5]: https://github.com/Tatsh/bfcodec/releases/tag/v0.0.5
[0.0.4]: https://github.com/Tatsh/bfcodec/releases/tag/v0.0.4
[0.0.3]: https://github.com/Tatsh/bfcodec/releases/tag/v0.0.3
[0.0.2]: https://github.com/Tatsh/bfcodec/releases/tag/v0.0.2
[0.0.1]: https://github.com/Tatsh/bfcodec/releases/tag/v0.0.1
