# `src/sources` — music sources

A group of modules, one per music source. Each source turns its service's (or, later, its files')
tracks into the `Core::Player`'s queue: what the user picks, what an endless queue plays next, the
feedback its service wants. This folder has no code of its own. The core knows no source by name
([#25](https://github.com/Kickoman/QiYaa/issues/25)): a source module sits above
[src/core](../core/README.md) and below [src/jam](../jam/README.md), and links its own client (for
Yandex Music, [src/yandex](../yandex/README.md)).

| Module | Namespace, library | What it is |
|---|---|---|
| [yandex](yandex/README.md) | `YandexSource`, `qiyaa_sources_yandex` | Yandex Music: the catalogue (likes, playlists, artists, albums, vibes, search) and the jam's own vibe |
| [local](local/README.md) | `LocalSource`, `qiyaa_sources_local` | The user's own files: their tags and covers, through TagLib |

A new source is a folder here with its README, a row in the table above and in the module table of
[docs/architecture.md](../../docs/architecture.md#modules), and `qiyaa_add_module(sources_<name> …)`
in the top-level `CMakeLists.txt`.

Only `jam`, `ui` and `app` include a source; a source includes none of the modules above it.

```bash
grep -rl --include='*.h' --include='*.cpp' --include='*.mm' '#include "sources/' src/ | grep -v '^src/\(sources\|jam\|ui\|app\)/'   # must print nothing
grep -rlE --include='*.h' --include='*.cpp' '#include "(jam|ui|integrations|app)/' src/sources/                                 # must print nothing
```
