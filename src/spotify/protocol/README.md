# `src/spotify/protocol` — Spotify's client protocol, without Qt

The protocol a Spotify client speaks, from a login to a decrypted Ogg Vorbis file
([spec/player/spotify.md](../../../spec/player/spotify.md)), in standard C++17, so that the Android app can use the
same code through JNI ([Kickoman/QiYaa-android#113](https://github.com/Kickoman/QiYaa-android/issues/113)). It never
opens a socket, makes an HTTP request, starts a thread or reads the clock (sans-IO): the caller moves the bytes,
sends the HTTP bodies it builds, hands it the time and owns the threads. Crypto is a subset of BearSSL
([contrib/bearssl](../../../contrib/bearssl), 7bea48e); only the protobuf wire format is our own. The reference is
[librespot](https://github.com/librespot-org/librespot) (MIT). Namespace `SpotifyProtocol`, library
`qiyaa_spotify_protocol`.

| File | What it is |
|---|---|
| `access_point.h/.cpp` | `AccessPoint`: the TCP session with an access point (AP). The handshake (DH over Oakley group 1, the RSA signature of the server's key, HMAC-SHA1 key derivation), Shannon-framed packets, the login with stored credentials or an OAuth token, the keep-alive, audio keys, the account's country and product. `Credentials`, `ClientIdentity`, `ServerKey`, `LoginError`, the events |
| `login.h/.cpp` | Bodies and replies of `clienttoken.spotify.com/v1/clienttoken` and `login5.spotify.com/v3/login`, the platform data client-token wants, and the hashcash both may ask for |
| `metadata.h/.cpp` | `extended-metadata` requests and replies (`TRACK_V4`, `ALBUM_V4`, `ARTIST_V4`, many at once), a track's artists, album and covers, an album's tracks, an artist's top tracks, the Ogg Vorbis file to play for a bitrate, `storage-resolve` reply |
| `catalog.h/.cpp` | The library and the lists: `collection/v2/paging` request and reply, a playlist or the rootlist (`playlist4` `SelectedListContent`), the autoplay request |
| `audio_file.h/.cpp` | `AudioDecryptor` (AES-128-CTR with Spotify's fixed IV, from any byte offset), the 167-byte Spotify header, the normalisation values in it |
| `crypto.h/.cpp`, `modpow.h/.c` | SHA-1, HMAC-SHA1, RSA PKCS#1 v1.5 check, modular power, AES-CTR over BearSSL. `modpow.c` is C because BearSSL's `inner.h` is not valid C++ |
| `shannon.h/.cpp` | The Shannon stream cipher and MAC, after the `shannon` crate (MIT) |
| `protobuf.h/.cpp` | `ProtoWriter`, `ProtoReader`: the wire format only, no schema |
| `ids.h/.cpp`, `bytes.h/.cpp` | Base62 track ids, hex, base64, big-endian helpers |

## Dependencies

- `qiyaa_spotify_protocol` links PRIVATE `qiyaa_bearssl` and nothing else: no Qt, no other module. BearSSL's headers
  appear only in `crypto.cpp` and `modpow.c`.
- `qiyaa_bearssl` is BearSSL 7bea48e's AES (constant-time), SHA-1, HMAC, RSA PKCS#1 check and i31 big integers,
  25 sources of `contrib/bearssl/src`, without the project's warning flags.

```bash
grep -rnE --include='*.h' --include='*.cpp' --include='*.c' '#include <Q|#include "(app|audio|core|integrations|jam|skins|sources|ui|vis|yandex)/' src/spotify/protocol/   # must print nothing
grep -rlE '#include [<"](bearssl|inner)' src/ | grep -vxE 'src/spotify/protocol/(crypto\.cpp|modpow\.c)'                 # must print nothing
grep -rnw --include='*.h' --include='*.cpp' --include='*.c' 'throw\|catch' src/spotify/protocol/                         # must print nothing
```

## `access_point.h`: `AccessPoint`

```cpp
class AccessPoint {
public:
    AccessPoint(ClientIdentity identity, Credentials credentials, TRandom random, ServerKey serverKey = SpotifyServerKey());
    void start();                                     // the client hello goes to takeOutgoing()
    void receive(ByteView bytes, std::int64_t nowMs); // whatever came from the socket, in any pieces
    void advance(std::int64_t nowMs);                 // the keep-alive: call at deadline()
    std::optional<std::int64_t> deadline() const;     // when advance() is due, while logged in
    TBytes takeOutgoing();                            // what to write to the socket
    std::vector<TApEvent> takeEvents();
    bool loggedIn() const;
    bool failed() const;                              // the connection is dead: close it, start a new one
    std::optional<std::uint32_t> requestAudioKey(const TGid& track, const TFileId& file);  // its sequence
};
```

- One object is one TCP connection. After every call the caller sends `takeOutgoing()`, handles `takeEvents()`
  and arms a timer for `deadline()`. Nothing is thread-safe: one thread uses it.
- **Login.** `Credentials::FromAccessToken` logs in with an OAuth token (SPOT-03); `ApLoggedIn` then carries the
  reusable credentials (username, type, blob) to keep instead of the token. A refused login is `ApLoginFailed` with
  Spotify's `LoginError` (`BadCredentials` SPOT-04, `PremiumAccountRequired` SPOT-05, `TryAnotherAp`), from the
  handshake or after it; the session is then failed.
- **Keep-alive** (SPOT-18), librespot's: after the login a Ping must come within `kFirstPingMs`; a Ping is answered
  by a Pong `kPongDelayMs` later; the PongAck must come within `kPongAckMs`, and the next Ping within
  `kNextPingMs`. A missed one is `ApFailure`.
- **Audio keys** (SPOT-16, SPOT-17): `ApAudioKey` or `ApAudioKeyRefused` with the request's sequence and the two code
  bytes. Retries and what a code means for playback are the caller's.
- `ApCountry` and `ApProduct` (the `<type>` of ProductInfo, `premium` or `free`) come once after the login.
- `ServerKey` exists for tests: anything but `SpotifyServerKey()` talks to a fake AP.

## `metadata.h`, `catalog.h`: the catalogue

Bodies to send and replies to read; the HTTP is the caller's ([src/spotify/client](../client/README.md)).
Checked live on 2026-10-21 against one account.

- `MetadataRequest(uris, kind)`: one `extended-metadata` request for many entities. `ParseMetadataReply` gives each
  entity's URI, status and payload (the `Any`'s value). Match the payloads by URI: the reply's order is not the
  request's. A status other than 200, or an empty payload, means the entity is gone.
- `ParseTrack`: the name, the artists, the album (name, year, covers), the duration, the files and the alternatives.
  A track the account's country may not play comes without files and with an alternative that has them; a track
  with neither is not available (SPOT-15): `PickOggVorbis` returns nothing for it.
- `Image::pixels` is the image's width when Spotify gives it, else its size class (64, 300, 640, 1280). Covers are
  `https://i.scdn.co/image/<hex of the id>`.
- `ParseAlbum`: the album's track ids, disc by disc in order; their metadata is a `TRACK_V4` request.
  `ParseArtist` and `TopTracksIn(artist, country)`: the top tracks of the country, else the worldwide `ZZ` ones,
  else those without a country.
- `CollectionPageRequest(username, set, pageToken, limit)` for `POST /collection/v2/paging` with
  `kCollectionContentType` as both `Content-Type` and `Accept`. Sets: `collection` holds the liked tracks and the
  saved albums (told apart by the URI), `artist` the followed artists; a set Spotify does not know answers 403
  (`albums` does). `ParseCollectionPage` drops removed items; an empty `nextPageToken` is the last page.
- `ParseListPage` reads `GET /playlist/v2/playlist/<id>` and `/playlist/v2/user/<username>/rootlist`: the name, the
  length, where the page starts, whether more follow, the items. With `decorate=attributes` each item of the
  rootlist gets its name, and items whose status is not 200 are left out. `PlaylistsOf` keeps the playlists of a
  rootlist, folders flattened.
- `AutoplayRequest(contextUri, recentTrackUris)` for `POST /context-resolve/v1/autoplay`; the reply is JSON.

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
| client-token | client version, platform data | `1.2.52.442`; on Linux the kernel release and version and the CPU; on Windows its version and build, platform 2 and the machine types of librespot; on macOS its version, `iMac21,1` and the CPU, as librespot |
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
