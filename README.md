# bfcodec

<!-- WISWA-GENERATED-README:START -->

[![C++](https://img.shields.io/badge/C++-00599C?logo=c%2B%2B)](https://isocpp.org)
[![GitHub tag (with filter)](https://img.shields.io/github/v/tag/Tatsh/bfcodec)](https://github.com/Tatsh/bfcodec/tags)
[![License](https://img.shields.io/github/license/Tatsh/bfcodec)](https://github.com/Tatsh/bfcodec/blob/master/LICENSE.txt)
[![GitHub commits since latest release (by SemVer including pre-releases)](https://img.shields.io/github/commits-since/Tatsh/bfcodec/v0.0.6/master)](https://github.com/Tatsh/bfcodec/compare/v0.0.6...master)
[![Dependabot](https://img.shields.io/badge/Dependabot-enabled-blue?logo=dependabot)](https://github.com/dependabot)
[![GitHub Pages](https://github.com/Tatsh/bfcodec/actions/workflows/pages.yml/badge.svg)](https://tatsh.github.io/bfcodec/)
[![Stargazers](https://img.shields.io/github/stars/Tatsh/bfcodec?logo=github&style=flat)](https://github.com/Tatsh/bfcodec/stargazers)
[![pre-commit](https://img.shields.io/badge/pre--commit-enabled-brightgreen?logo=pre-commit)](https://github.com/pre-commit/pre-commit)
[![CMake](https://img.shields.io/badge/CMake-6E6E6E?logo=cmake)](https://cmake.org/)
[![Prettier](https://img.shields.io/badge/Prettier-black?logo=prettier)](https://prettier.io/)
[![Tests](https://github.com/Tatsh/bfcodec/actions/workflows/tests.yml/badge.svg)](https://github.com/Tatsh/bfcodec/actions/workflows/tests.yml)
[![Coverage Status](https://coveralls.io/repos/github/Tatsh/bfcodec/badge.svg?branch=master)](https://coveralls.io/github/Tatsh/bfcodec?branch=master)

[![@Tatsh](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fpublic.api.bsky.app%2Fxrpc%2Fapp.bsky.actor.getProfile%2F%3Factor=did%3Aplc%3Auq42idtvuccnmtl57nsucz72&query=%24.followersCount&label=Follow+%40Tatsh&logo=bluesky&style=social)](https://bsky.app/profile/Tatsh.bsky.social)
[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-Tatsh-black?logo=buymeacoffee)](https://buymeacoffee.com/Tatsh)
[![Libera.Chat](https://img.shields.io/badge/Libera.Chat-Tatsh-black?logo=liberadotchat)](irc://irc.libera.chat/Tatsh)
[![Mastodon Follow](https://img.shields.io/mastodon/follow/109370961877277568?domain=hostux.social&style=social)](https://hostux.social/@Tatsh)
[![Patreon](https://img.shields.io/badge/Patreon-Tatsh2-F96854?logo=patreon)](https://www.patreon.com/Tatsh2)

<!-- WISWA-GENERATED-README:STOP -->

Tools and a C/C++ library to manipulate BFCodec-encrypted content.

## How to use

### jbt

`jbt` encrypts a directory of game data then zips it up. The extension depends on the target game. You
must use the correct key to encrypt the data.

```shell
jbt -K 'the game key' game-data/ game-data.jbt
```

### unjbt

Like `unzip`, `unjbt` extracts the zip file then decrypts each game file. Conversions will happen if
tools are in `PATH`:

| Input Format                 | Output Format     | Tool Used                                                         |
| ---------------------------- | ----------------- | ----------------------------------------------------------------- |
| Binary Property List (plist) | XML Property List | `libplist` (if found at configure time), `CoreFoundation` (macOS) |
| CgBI PNG                     | PNG               | `pngcrush`, `pngdefry`                                            |

pngdefry can be downloaded from [its official site (archived)](https://web.archive.org/web/20211120053356/http://www.jongware.com/pngdefry.html).
`pngcrush` is only available on macOS if you have Xcode installed.

```shell
unjbt -d game-data game-data.jbt
```

If the directory passed to `-d`/`--extract-to` does not exist it will be created.

### bfc

`bfc` encrypts a single file in place, overwriting it with the encrypted data. The same key and IV
must be supplied to decrypt it again with `unbfc`.

```shell
bfc -K 'the game key' game-file
```

Pass `-o`/`--output` to write to a different path instead of overwriting the input. With
`--backup`, an existing destination is first copied to `<dest>.bak`.

Pass `--uuid` instead of `-K` to derive the key from a device UUID. The key is the MD5 of the
UUID's canonical uppercase form, so dashes and letter case are optional.

### unbfc

`unbfc` reverses `bfc`. It decrypts a file in place, overwriting it with the decrypted data. The key
and IV must match those used to encrypt the file.

```shell
unbfc -K 'the game key' game-file
```

Pass `-o`/`--output` to write to a different path instead of overwriting the input. With
`--backup`, an existing destination is first copied to `<dest>.bak`.

Some iOS games key files to a per-device UUID (for example `mulist` and `prodlist`). Pass that UUID
with `--uuid`; the key is the MD5 of its canonical uppercase form, so dashes and letter case are
optional:

```shell
unbfc --uuid 1ac4f2e0-9b3d-4c77-ae51-2d9f8c0a77b1 mulist
```

### C interface

```c
// If using a DLL on Windows:
// #ifdef _WIN32
// #define BFCODEC_USE_DLL 1
// #endif
#include <bfcodec.h>

void func() {
  C_BLOWFISH *blf = bfcodec_init();
  const uint8_t my_key = {};
  const uint8_t iv[8] = {};
  bfcodec_expand_key(blf, &my_key, 16);
  bfcodec_decrypt(blf, in_out_data, data_len, my_iv);
  // or
  bfcodec_encrypt(blf, in_out_data, data_len, my_iv);
}
```

Link with `-lbfcodec`.

### C++ interface

```c++
// If using a DLL on Windows:
// #ifdef _WIN32
// #define BFCODEC_USE_DLL 1
// #endif
#include <bfcodecpp.h>

void func() {
  auto bfc = BFCodec::create();
  bfc.expandKey({ /* key here */});
  bfc.decrypt(inOutData, {/* IV here */});
  // or
  bfc.encrypt(inOutData, {/* IV here */})
}
```

Link with `-lbfcodec`.

## Building from source

Requirements:

- CMake
- On Linux (for tools): OpenSSL
- Optional: libplist on non-macOS

Basic commands to build after cloning:

```shell
mkdir build
cd build
cmake ..
make
```

`jbt` and `unjbt` will be in the `tools` directory.
