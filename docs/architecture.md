# Architecture

A map of the code for someone about to change it. Each module folder has a README with the
file-level reference; this page only says how the modules fit together and where to start. The
rules for changing the code are [code-style.md](code-style.md) and, where QiYaa departs from them,
[CLAUDE.md](../CLAUDE.md).

## Modules

A folder of `src/` with files of its own is one module: one namespace, one static library
`qiyaa_<folder>`, one README. A folder that holds only folders is a group: its README is an index,
and each folder in it is a module named after both. In `src/sources/` each music source is a
module `src/sources/<name>` with the namespace `<Name>Source` and the library
`qiyaa_sources_<name>`; in `src/spotify/` each part of the Spotify client is a module
`src/spotify/<part>` with the namespace `Spotify<Part>` and the library `qiyaa_spotify_<part>`. Dependencies point one way, and a module never includes a header of a
module to its right:

```
audio, yandex, spotify/protocol, skins, telemetry  →  vis, core, spotify/client  →  sources/yandex, sources/local, lrclib  →  jam  →  ui, integrations  →  app  →  qiyaa (src/app/main.cpp)
```

| Module | Namespace | What it does | Links PUBLIC | Links PRIVATE |
|---|---|---|---|---|
| [src/audio](../src/audio/README.md) | `Audio` | Plays a file while it downloads: decoder thread, blocks of audio with what they are, equalizer, gapless chaining, `.eqf` presets; AAC in MP4 for the economy quality, Ogg Vorbis for Spotify and local files | Qt Core | miniaudio, minimp4, fdk-aac, stb_vorbis |
| [src/yandex](../src/yandex/README.md) | `Yandex` | Yandex Music HTTP API, OAuth device login, signed track links, the token file | Qt Core, Network | |
| [src/spotify/protocol](../src/spotify/protocol/README.md) | `SpotifyProtocol` | Spotify's client protocol in standard C++ without Qt, shared with Android (sans-IO): the access point session (handshake, login, keep-alive, audio keys), client-token and login5 bodies, track metadata, decrypting the files | | BearSSL |
| [src/spotify/client](../src/spotify/client/README.md) | `SpotifyClient` | Spotify on Qt: the logins (a code at spotify.com/pair, the browser), the access point connection with its keep-alive, reconnects and audio keys, the client-token and login5 tokens and `spclient` requests, a track's file from the CDN, decrypted | spotify/protocol, Qt Core, Network | |
| [src/skins](../src/skins/README.md) | `Skins` | Loads `.wsz` skins: sheets, `region.txt`, `pledit.txt`, `viscolor.txt`, bitmap text; sprite tables | Qt Gui | miniz |
| [src/telemetry](../src/telemetry/README.md) | `Telemetry` | Usage statistics and crash reports ([spec/telemetry](../spec/telemetry/README.md)): the events kept on disk until the jam server takes them, the crash handler. Optional (`QIYAA_WITH_TELEMETRY`, on in the releases) | Qt Core, Network | Qt Gui; dbghelp (Windows) or dl |
| [src/vis](../src/vis/README.md) | `Vis` | Spectrum, oscilloscope, FFT; Milkdrop through projectM in a `QOpenGLWindow` | Qt Gui (+ OpenGL) | audio, skins (+ projectM) |
| [src/core](../src/core/README.md) | `Core` | `Track` and `TrackKey` (a track of any source), `TrackSource` and `SourceRegistry` (what a source answers about its tracks, and how it opens their files), `Player` (queue, transport, downloads, preload, track events), `JamMode` (the jam over the host's queue, as the queue's layer) and `CoverCache`; `LyricsFinder` (the lyrics the lyrics view shows, from the track's source and a lyrics service) | audio, Qt Gui, Network | |
| [src/sources/yandex](../src/sources/yandex/README.md) | `YandexSource` | Yandex Music as a source: `Yandex::Track` ↔ `Core::Track`, its `TrackSource` (cover, web page, the file and its quality, the play report, the lyrics from `/supplement`), `Catalog` (what a pick plays, likes), and the continuations `Vibe` (a vibe or station) and `HistoryWave` (the vibe after a queue's head, the jam vibe during a jam) | core, yandex, Qt Core | |
| [src/sources/local](../src/sources/local/README.md) | `LocalSource` | The user's own files as a source: `Catalog` (opening files and folders on a thread of its own), their tags and covers through TagLib with every size bounded, and its `TrackSource`, which hands the engine the file on disk and reads a file's lyrics from its `.lrc` and its tags | core, Qt Core | TagLib |
| [src/lrclib](../src/lrclib/README.md) | `Lrclib` | LRCLIB, the lyrics service the lyrics view asks when the user turns it on: the search, the record that fits the track, one retry, the size bound | core, Qt Core, Network | |
| [src/jam](../src/jam/README.md) | `Jam` | The jam: protocol messages and their JSON, the WebSocket connection to a jam server, the host's stored session, and `HostSession` (create, resume, the guests' search and checks, end). Optional (Qt WebSockets) | core, yandex, Qt Core, WebSockets | sources/yandex |
| [src/ui](../src/ui/README.md) | `Ui` | The skinned windows (the lyrics window among them), snapping and docking, the Yandex and Spotify menus, the login dialogs, the jam window | audio, vis, Qt Widgets (+ jam) | core, skins, sources/yandex, spotify/client, yandex (+ qrcodegen) |
| [src/integrations](../src/integrations/README.md) | `Integrations` | Media keys and system media panels: MPRIS (Linux), SMTC (Windows) or MediaPlayer (macOS) | Qt Gui (+ DBus) | core |
| [src/app](../src/app/README.md) | `App` | `Application`: owns and wires everything, decides nothing; the controllers `Settings`, `WindowSet`, `MainMenu`, `Account`, `SpotifyAccount`, `JamBridge`, `TelemetryBridge`, `NetworkWatch`; `main()` | audio, core, lrclib, skins, sources/local, sources/yandex, spotify/client, yandex | integrations, jam, ui, vis (+ telemetry) |

Vendored code lives in `contrib/` (miniaudio, miniz, qrcodegen, minimp4, fdk-aac's decoder, stb_vorbis, BearSSL) and is
not changed. TagLib is fetched at configure time and linked statically
([THIRD_PARTY.md](../THIRD_PARTY.md)). The interface texts are English sources in the code,
translated into Belarusian (the default) and Russian in [translations/](../translations/README.md). `spec/` is a submodule
with the behaviour and test data shared with the Android app ([spec/README.md](../spec/README.md)).
Milkdrop (`QIYAA_HAVE_MILKDROP`), the jam (`QIYAA_HAVE_JAM`), telemetry (`QIYAA_HAVE_TELEMETRY`),
MPRIS (`QIYAA_HAVE_MPRIS`) and SMTC (`QIYAA_HAVE_SMTC`) are optional; the macOS media controls
(`QIYAA_HAVE_MAC_MEDIA_CONTROLS`) are always built on macOS. The build options are in
[building.md](building.md#параметры-cmake).

## Who owns what

`App::Application` is the composition root: it creates, owns and connects, and decides nothing. It
holds `App::Settings`, the skins, the network manager, `Yandex::ApiClient`, `Yandex::Library`,
`Audio::AudioEngine`, `YandexSource::Source`, `LocalSource::Source`, `Core::SourceRegistry`,
`Core::Player`, `YandexSource::Catalog`, `LocalSource::Catalog`, `Core::JamMode`,
`Core::LyricsFinder` and `App::NetworkWatch` as members, `Lrclib::Service` through a
`std::unique_ptr` (none in an offline run), and the windows, the
cover cache, the media integration and the controllers (`WindowSet`, `Account`, `JamBridge`,
`TelemetryBridge`, `MainMenu`) through `std::unique_ptr`. Member order is the construction order
and, reversed, the destruction order: the controllers go first, then the windows, while everything
they point at still exists. Nothing below `app` creates or connects windows, reads `QSettings` or
knows where files live, and in `app` only `App::Settings` touches `QSettings`. Details:
[src/app](../src/app/README.md).

## Playing a track

```
Ui (menu)  ──source──▶  Core::Player  ──open──▶  Core::SourceRegistry  ──▶  YandexSource::Source  ──▶  Yandex::ApiClient
                            │  ▲
          download bytes    │  │ poll(): state, trackFinished, trackAdvanced
                            ▼  │
                      Audio::AudioEngine ─▶ decoder thread ─▶ blocks (2 s) ─▶ device callback
                                                                    EQ → VisTap → volume, balance
```

1. A menu item from `Ui::AddLibraryActions` calls `YandexSource::Catalog`, which asks
   `Yandex::Library` for a source (a playlist, the likes, a wave) and hands it to
   `Player::setSource` as a `Core::QueueSource`: a head of `Core::Track`s, keyed by source and the
   source's own id, and for a wave a continuation (`YandexSource::Vibe`) that fetches more tracks
   and sends the wave feedback for the tracks it gave. Any other queue goes on, when its flag says
   so, with the vibe from history (`YandexSource::HistoryWave`, from the factory the app gives the
   `Player`); during a jam that is the jam vibe, under the guests' items. "Open files…" and "Open
   folder…" (`App::MainMenu`) call `LocalSource::Catalog` instead, which reads the files on a thread
   of its own and hands them over the same way, as a queue without a vibe from history.
2. `Player::playIndex` starts a stream in the engine and asks the track's source, through
   `SourceRegistry`, to open the track. `YandexSource::Source` resolves the link of the file in the
   chosen quality (a signed mp3 link, or AAC in MP4 at the economy quality) and hands back a
   download that has not started yet. The `Player` starts it and appends it to the stream chunk by
   chunk. When the download completes, it opens and downloads the next track into a queued stream,
   so the engine continues without a gap. A source may hand back a file on disk instead
   (`Audio::FileByteSource`): the engine reads it in place, and the next track is opened at once.
3. The engine decodes on its own thread into a queue of blocks, each saying which track, where in
   it and for which start or seek it is; miniaudio's device thread plays them, applies the
   equalizer, copies the frames into `VisTap` and applies volume and balance.
4. The main window's timer reads `VisTap` through the engine and draws the spectrum or the
   oscilloscope with `Vis`; the Milkdrop window feeds the same samples to projectM.
5. `Integrations::MediaControls` turns `Player`'s state into what MPRIS, SMTC or MediaPlayer
   expect and their commands into `Player` calls.

## Threads

| Thread | What runs there |
|---|---|
| Qt main thread | Everything outside `src/audio` but a local scan and a local file's lyrics: every window, every network callback, `Player`, the engine's public API and signals |
| A local folder's scan (one at a time, `LocalSource::Catalog`) | Lists the files of a pick, reads their tags and writes their covers; hands the tracks back to the main thread and ends |
| A local file's lyrics (one per lookup, `LocalSource::Source`) | Reads the `.lrc` next to the file and the lyrics in its tags; hands the answer back to the main thread and ends |
| Decoder thread (one per engine) | Owns every `ma_decoder`, decides whether a track ends or the queued one follows, writes the blocks; takes commands from the engine and sends it events |
| Device thread (miniaudio's) | Plays the blocks, equalizer, `VisTap`, gains; takes no lock and allocates nothing |
| WinRT thread (Windows) | SMTC button presses, handed straight to the main thread |

Each audio thread owns its own state and the three talk only through queues; the channels are in
[src/audio](../src/audio/README.md#threads).

## Errors

Two modules throw: [src/skins](../src/skins/README.md#errors) (`Skins::Error` for a broken `.wsz`)
and [src/audio](../src/audio/README.md#errors) (`Audio::Error` for a broken `.eqf`). Both loaders
are synchronous and their callers in `src/app` and `src/ui` catch the error and show its message.
The vendored QR code generator throws a third, `qrcodegen::data_too_long`, which `Ui::JamWindow`
catches where it makes the code.
An exception never crosses Qt's event loop. Everything else — a network failure, a missing link, an
audio device that does not open — is data: an error string in a callback or a signal, an
`InitResult`, an empty `std::optional`. `main` catches whatever escapes as a bug and exits with 2
([cli.md](cli.md#коды-выхода)).

## Where to start

| To change… | Start in |
|---|---|
| a window's look or behaviour, snapping, shade mode, scaling | [src/ui](../src/ui/README.md) |
| sprite positions, skin parsing, bitmap text | [src/skins](../src/skins/README.md) |
| what plays next, shuffle, repeat, preload, track events | [src/core](../src/core/README.md) |
| decoding, seeking, gapless, the equalizer DSP | [src/audio](../src/audio/README.md) |
| an API call, a new pick, login | [src/yandex](../src/yandex/README.md), then [src/sources/yandex](../src/sources/yandex/README.md) and the menu in `src/ui/library_menu.cpp` |
| a new music source | [src/sources](../src/sources/README.md) |
| the spectrum, the oscilloscope, Milkdrop rendering | [src/vis](../src/vis/README.md) |
| lyrics: where they come from and in which order | [spec/player/lyrics.md](../spec/player/lyrics.md), then `LyricsFinder` in [src/core](../src/core/README.md), a source's `lyrics`, [src/lrclib](../src/lrclib/README.md) |
| media keys, MPRIS, SMTC, MediaPlayer | [src/integrations](../src/integrations/README.md) |
| usage statistics, crash reports | [spec/telemetry](../spec/telemetry/README.md), then [src/telemetry](../src/telemetry/README.md) |
| menus, shortcuts, settings, start-up and shut-down order, command-line options | [src/app](../src/app/README.md) |
| tests, golden screenshots, the mock HTTP server | [tests](../tests/README.md) |
