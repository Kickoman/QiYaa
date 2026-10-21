# `src/spotify` — the Spotify protocol core

Spike for [#52](https://github.com/Kickoman/QiYaa/issues/52). The protocol a Spotify client speaks, from stored
credentials to a decrypted Ogg Vorbis file, in standard C++ with no Qt, so that the Android app can use the same
code through JNI. It never opens a socket or makes an HTTP request (sans-IO): the caller moves the bytes, keeps the
timers and owns the threads. Crypto is BearSSL ([contrib/bearssl](../../contrib/bearssl), a subset of 7bea48e);
only protobuf is our own. The reference is [librespot](https://github.com/librespot-org/librespot) (MIT).

| File | What it is |
|---|---|
| `access_point.h/.cpp` | `AccessPoint`: the TCP session with an access point. Handshake (DH over Oakley group 1, the server key's RSA signature, HMAC-SHA1 key derivation), Shannon-framed packets, login with stored credentials, ping/pong, audio keys. `receive` bytes in, `takeOutgoing` bytes out, `takeEvents` what happened |
| `login.h/.cpp` | Bodies and replies of `clienttoken.spotify.com/v1/clienttoken` and `login5.spotify.com/v3/login`, and the hashcash both may ask for |
| `metadata.h/.cpp` | `extended-metadata` (`TRACK_V4`) request and reply, the Ogg Vorbis file to play, `storage-resolve` reply |
| `audio_file.h/.cpp` | `AudioDecryptor` (AES-128-CTR with Spotify's fixed IV, from any byte offset), the 167-byte Spotify header, the normalisation values in it |
| `crypto.h/.cpp`, `modpow.h/.c` | SHA-1, HMAC-SHA1, RSA PKCS#1 v1.5 check, modular power, AES-CTR over BearSSL. `modpow.c` is C because BearSSL's `inner.h` is not valid C++ |
| `shannon.h/.cpp` | The Shannon stream cipher and MAC, after the `shannon` crate (MIT) |
| `protobuf.h/.cpp` | `ProtoWriter`, `ProtoReader`: the wire format only, no schema |
| `ids.h/.cpp`, `bytes.h/.cpp` | Base62 track ids, hex, base64, big-endian helpers |

## What QiYaa tells Spotify about itself

Decided on 2026-10-21 ([#52](https://github.com/Kickoman/QiYaa/issues/52)): free-form strings name QiYaa, and every
field the servers check is what librespot sends, because librespot has been tested against those checks for years.
The values below were accepted live on 2026-10-21 by QiYaa 0.7.2: access point login, an audio key, client-token
and login5.

| Where | Field | Value |
|---|---|---|
| Everywhere | Client ID | `65b708073fc0480ea92a077233ca87bd`, Spotify's desktop client. Since 2026-08-10 login5 refuses stored credentials made with any other ID ([librespot#1737](https://github.com/librespot-org/librespot/issues/1737)) |
| Access point, `ClientHello.build_info` | product, product flags, platform, version | `PRODUCT_CLIENT`, none, `ApPlatform` by OS and CPU as in librespot, `124200290` |
| Access point, login | `system_info.cpu_family`, `os` | as in librespot |
| Access point, login | `system_info.system_information_string`, `version_string` | `QiYaa X.Y.Z` |
| Access point, login, client-token | device ID | 20 random bytes as 40 hex digits, made once per installation and kept. librespot hashes the device name instead, which would give every QiYaa the same ID |
| client-token | client version, platform data | `1.2.52.442`; on Linux the kernel release and version and the CPU |
| HTTP | `User-Agent` | `Spotify/124200290 Linux/0 (QiYaa X.Y.Z)` |

If Spotify starts refusing third-party clients:

1. Our own strings are the first thing to try changing. librespot's were accepted on 2026-10-21: system
   information `librespot-939dc5ee-cpOf1Eau`, version `librespot 0.8.0`, user agent
   `Spotify/124200290 Linux/0 (librespot-939dc5ee)`.
2. The checked fields follow librespot's `dev` branch: `core/src/config.rs` (client IDs), `core/src/version.rs`
   (version numbers), `core/src/connection/handshake.rs` (platform IDs). Known checks so far: the client ID
   (librespot#1737, 2026-08-10) and the platform ID (Windows on ARM refused,
   [librespot#1761](https://github.com/librespot-org/librespot/issues/1761), 2026-09).
3. A fix usually appears first in librespot's issues ([#1649](https://github.com/librespot-org/librespot/issues/1649)
   for audio keys, #1737 for login) or go-librespot's (read only: GPL, no code from there).

## Traps

- The handshake's transcript is every byte of the client hello (with its `00 04` prefix and length) and of the
  server's reply (with its length). The shared secret and the public key lose their leading zero bytes, as librespot's
  `to_bytes_be` does, or one handshake in 256 fails.
- Received packets are decrypted as a 3-byte header and then the payload, under one nonce. The Shannon MAC of a word
  split over two calls follows the C reference: one MAC step when the word completes.
- BearSSL's CTR mode counts in the last 32 bits of the IV. Spotify's counter is the whole 128-bit IV, so
  `AesCtr` splits a run where the low word wraps and carries into the upper bytes itself.
- An audio key comes over the access point, not HTTP: `AesKeyError` code 1 refuses the account for good, code 2 is
  worth one retry a second later.
