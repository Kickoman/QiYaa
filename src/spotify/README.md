# `src/spotify` — Spotify

A group of modules for Spotify as a music source ([#52](https://github.com/Kickoman/QiYaa/issues/52),
[spec/player/spotify.md](../../spec/player/spotify.md)). This folder has no code of its own.

| Module | Namespace, library | What it is |
|---|---|---|
| [protocol](protocol/README.md) | `SpotifyProtocol`, `qiyaa_spotify_protocol` | Spotify's client protocol in standard C++ without Qt, shared with Android: the access point session, client-token and login5, track metadata, audio keys, decrypting the files |
| [client](client/README.md) | `SpotifyClient`, `qiyaa_spotify_client` | Spotify's client on Qt Core and Network: the logins (a code, the browser), the access point connection and its keys, the web tokens and requests, a track's file from the CDN |

The source itself, with the catalogue and the menus, comes next as `src/sources/spotify`.
