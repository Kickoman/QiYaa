# Architecture

A map of the code for someone about to change it. Each module folder has a README with the
file-level reference; this page only says how the modules fit together and where to start. The
rules for changing the code are [code-style.md](code-style.md) and, where QiYaa departs from them,
[CLAUDE.md](../CLAUDE.md).

## Modules

Every folder of `src/` is one module: one namespace, one static library `qiyaa_<folder>`, one
README. Dependencies point one way, and a module never includes a header of a module to its right:

```
audio, yandex, skins  →  vis, core  →  jam  →  ui, integrations  →  app  →  qiyaa (src/app/main.cpp)
```

| Module | Namespace | What it does | Links PUBLIC | Links PRIVATE |
|---|---|---|---|---|
| [src/audio](../src/audio/README.md) | `Audio` | Plays a file while it downloads: decoder thread, ring buffer, equalizer, gapless chaining, `.eqf` presets | Qt Core | miniaudio |
| [src/yandex](../src/yandex/README.md) | `Yandex` | Yandex Music HTTP API, OAuth device login, signed track links, the token file | Qt Core, Network | |
| [src/skins](../src/skins/README.md) | `Skins` | Loads `.wsz` skins: sheets, `region.txt`, `pledit.txt`, `viscolor.txt`, bitmap text; sprite tables | Qt Gui | miniz |
| [src/vis](../src/vis/README.md) | `Vis` | Spectrum, oscilloscope, FFT; Milkdrop through projectM in a `QOpenGLWindow` | Qt Gui (+ OpenGL) | audio, skins (+ projectM) |
| [src/core](../src/core/README.md) | `Core` | `Player` (queue, transport, downloads, preload, track events), `Sources` (what a pick plays, wave callbacks), `JamMode` (the queue while hosting a jam) and `CoverCache` | audio, yandex, Qt Gui, Network | |
| [src/jam](../src/jam/README.md) | `Jam` | The jam: protocol messages and their JSON, the WebSocket connection to a jam server, the host's stored session, and `HostSession` (create, resume, the guests' search and checks, end). Optional (Qt WebSockets) | core, yandex, Qt Core, WebSockets | |
| [src/ui](../src/ui/README.md) | `Ui` | The skinned windows, snapping and docking, the Yandex menus, the login dialog | audio, vis, Qt Widgets | core, skins, yandex |
| [src/integrations](../src/integrations/README.md) | `Integrations` | Media keys and system media panels: MPRIS (Linux) or SMTC (Windows) | Qt Gui (+ DBus) | audio, core, yandex |
| [src/app](../src/app/README.md) | `App` | `Application`: owns and wires everything, menus, shortcuts, settings, login, "Continue the jam?"; `main()` | audio, core, skins, yandex | integrations, jam, ui, vis |

Vendored code lives in `contrib/` (miniaudio, miniz) and is not changed. `spec/` is a submodule
with the behaviour and test data shared with the Android app ([spec/README.md](../spec/README.md)).
Milkdrop (`QIYAA_HAVE_MILKDROP`), the jam (`QIYAA_HAVE_JAM`), MPRIS (`QIYAA_HAVE_MPRIS`) and SMTC
(`QIYAA_HAVE_SMTC`) are optional; the build options are in [building.md](building.md#параметры-cmake).

## Who owns what

`App::Application` is the composition root. It holds the settings, the skins, the network manager,
`Yandex::ApiClient`, `Yandex::Library`, `Audio::AudioEngine` and `Core::Player` as members, and the
windows, the cover cache and the media integration through `std::unique_ptr`. Member order is the
construction order and, reversed, the destruction order: the windows go first, while everything
they point at still exists. Nothing below `app` creates or connects windows, reads `QSettings` or
knows where files live. Details: [src/app](../src/app/README.md).

## Playing a track

```
Ui (menu)  ──source──▶  Core::Player  ──resolveTrackUrl──▶  Yandex::ApiClient
                            │  ▲
          download bytes    │  │ poll(): state, trackFinished, trackAdvanced
                            ▼  │
                      Audio::AudioEngine ─▶ decoder thread ─▶ ring (2 s) ─▶ device callback
                                                                  EQ → VisTap → volume, balance
```

1. A menu item from `Ui::AddLibraryActions` calls `Core::Sources`, which asks `Yandex::Library`
   for a source (a playlist, the likes, a wave) and hands the tracks to `Player::setQueue`. An
   endless source also gives a callback that fetches more tracks; for a wave, `Sources` also
   sends wave feedback from `Player`'s track events.
2. `Player::playIndex` asks `ApiClient::resolveTrackUrl` for a signed mp3 link, starts a stream in
   the engine and appends the download to it chunk by chunk. When the download completes, it
   resolves and downloads the next track into a queued stream, so the engine continues without a
   gap.
3. The engine decodes on its own thread into a ring buffer; miniaudio's device thread reads the
   ring, applies the equalizer, copies the frames into `VisTap` and applies volume and balance.
4. The main window's timer reads `VisTap` through the engine and draws the spectrum or the
   oscilloscope with `Vis`; the Milkdrop window feeds the same samples to projectM.
5. `Integrations::MediaControls` turns `Player`'s state into what MPRIS or SMTC expect and their
   commands into `Player` calls.

## Threads

| Thread | What runs there |
|---|---|
| Qt main thread | Everything outside `src/audio`: every window, every network callback, `Player`, the engine's public API and signals |
| Decoder thread (one per stream start) | `ma_decoder` on the downloaded bytes, writes into the ring |
| Device thread (miniaudio's) | Reads the ring, equalizer, `VisTap`, gains; takes no lock and allocates nothing |
| WinRT thread (Windows) | SMTC button presses, handed straight to the main thread |

The hand-offs between the three audio threads are in [src/audio](../src/audio/README.md#threads).

## Errors

Two modules throw: [src/skins](../src/skins/README.md#errors) (`Skins::Error` for a broken `.wsz`)
and [src/audio](../src/audio/README.md#errors) (`Audio::Error` for a broken `.eqf`). Both loaders
are synchronous and their callers in `src/app` and `src/ui` catch the error and show its message.
An exception never crosses Qt's event loop. Everything else — a network failure, a missing link, an
audio device that does not open — is data: an error string in a callback or a signal, an
`InitResult`, an empty `std::optional`. `main` catches whatever escapes as a bug and exits with 2
([cli.md](cli.md#коды-выхода)).

## Where to start

| To change… | Start in |
|---|---|
| a window's look or behaviour, snapping, shade mode, scaling | [src/ui](../src/ui/README.md) |
| sprite positions, skin parsing, bitmap text | [src/skins](../src/skins/README.md) |
| what plays next, shuffle, repeat, preload, play reports | [src/core](../src/core/README.md) |
| decoding, seeking, gapless, the equalizer DSP | [src/audio](../src/audio/README.md) |
| an API call, a new source, login | [src/yandex](../src/yandex/README.md), then `src/core/sources.cpp` and the menu in `src/ui/library_menu.cpp` |
| the spectrum, the oscilloscope, Milkdrop rendering | [src/vis](../src/vis/README.md) |
| media keys, MPRIS, SMTC | [src/integrations](../src/integrations/README.md) |
| menus, shortcuts, settings, start-up and shut-down order, command-line options | [src/app](../src/app/README.md) |
| tests, golden screenshots, the mock HTTP server | [tests](../tests/README.md) |
