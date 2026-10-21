# `src/spotify` — Spotify

A group of modules for Spotify as a music source ([#52](https://github.com/Kickoman/QiYaa/issues/52),
[spec/player/spotify.md](../../spec/player/spotify.md)). This folder has no code of its own.

| Module | Namespace, library | What it is |
|---|---|---|
| [protocol](protocol/README.md) | `SpotifyProtocol`, `qiyaa_spotify_protocol` | Spotify's client protocol in standard C++ without Qt, shared with Android: the access point session, client-token and login5, track metadata, audio keys, decrypting the files |

The Qt client (connections, HTTP, login, the catalogue) and the source itself come next, as `src/spotify/client`
and `src/sources/spotify`.
