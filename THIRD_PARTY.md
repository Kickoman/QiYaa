# Third-party components

QiYaa itself is MIT-licensed (see `LICENSE`). It includes or links the following.

## Linked libraries

| Component | License | How it is used |
|---|---|---|
| [Qt 6](https://www.qt.io/) — Core, Gui, Widgets, Network, WebSockets, OpenGL, DBus (Linux) | LGPLv3 | Linked **dynamically**. You may replace the Qt libraries shipped with a build by your own. Qt sources: https://download.qt.io/official_releases/qt/ |
| [projectM](https://github.com/projectM-visualizer/projectm) 4.1.7 — the Milkdrop engine | LGPL-2.1 | Linked **dynamically** (`libprojectM-4.so` / `projectM-4.dll` / `.dylib`, shipped next to the app); replaceable by your own build of the same version. Its license text comes with every binary package as `projectM-LICENSE.txt`. Sources: https://github.com/projectM-visualizer/projectm/tree/v4.1.7 |
| [TagLib](https://taglib.org/) 2.3.2 — tags and covers of local files | MPL-1.1 (TagLib is offered under LGPL-2.1 or MPL-1.1; QiYaa takes MPL-1.1) | Linked **statically**, unmodified, built by QiYaa's CMake without zlib and with only the formats the engine plays. Its license text comes with every binary package as `taglib-LICENSE.txt`, next to this file. Sources: https://github.com/taglib/taglib/releases/tag/v2.3.2. It contains [utfcpp](https://github.com/nemtrif/utfcpp) (BSL-1.0), header-only |

Only LGPL-licensed Qt modules are used. GPL-only modules (Qt HTTP Server, Qt Network Authorization, etc.) are deliberately not used.

projectM's library contains its vendored components: [projectm-eval](https://github.com/projectM-visualizer/projectm-eval) (MIT),
[hlslparser](https://github.com/Thekla/hlslparser) (MIT), [SOIL2](https://github.com/SpartanJ/SOIL2) (MIT-0; with Apache-2.0 ETC1 and MIT PowerVR code)
and [GLM](https://github.com/g-truc/glm) (MIT). On Windows it uses [GLEW](https://github.com/nigels-com/glew) (Modified BSD / MIT), shipped as `glew32.dll`.

## Bundled source code

| Component | Version | License | Location |
|---|---|---|---|
| [miniaudio](https://github.com/mackron/miniaudio) — David Reid | 0.11.25 (`9634bed`) | Public domain / MIT-0 | `contrib/miniaudio/` |
| [miniz](https://github.com/richgel999/miniz) — Rich Geldreich, RAD Game Tools, Valve | 3.1.2 (`77d0dce`) | MIT | `contrib/miniz/` |
| [QR Code generator](https://github.com/nayuki/QR-Code-generator) (C++) — Project Nayuki | 1.8.0 (`7ad95ce`) | MIT | `contrib/qrcodegen/` (only in builds with the jam) |
| [minimp4](https://github.com/lieff/minimp4) — Lieff and contributors | `2a3014a` | CC0 1.0 | `contrib/minimp4/` |
| [FDK AAC](https://github.com/mstorsjo/fdk-aac) — Fraunhofer IIS, the decoder libraries only | 2.0.3 (`716f439`) | Software License for The Fraunhofer FDK AAC Codec Library for Android (see below) | `contrib/fdk-aac/` (`CMakeLists.txt` there is QiYaa's) |

**FDK AAC and patents.** Fraunhofer's license (`contrib/fdk-aac/LICENSE`, upstream's `NOTICE`)
allows redistributing the library in source and binary form, and QiYaa does so to play the economy
quality (HE-AAC). Every binary package carries the license's full text as `fdk-aac-LICENSE.txt`
(next to this file: `/usr/share/doc/qiyaa/` in the .deb and the AppImage, the program folder on
Windows, `Contents/Resources/` of the macOS app), and its source is this repository. Its section 3 grants **no patent license**: AAC and HE-AAC are
covered by patents licensed through Via LA or their owners, and the library "may be used only for
purposes that are authorized by appropriate patent licenses". QiYaa has no such license. The same
holds for any other AAC decoder that is not part of the operating system, FFmpeg's included. The
FDK license is not compatible with the GPL, which QiYaa does not use. Fraunhofer's name may not be
used to endorse or promote QiYaa. QiYaa does not change the library's code.

## Test data

| Component | Version | License | Location |
|---|---|---|---|
| [Tiny5](https://github.com/Gissio/font_tiny5) — The Tiny5 Project Authors | 1.002 (google/fonts `b272357`) | SIL Open Font License 1.1 | `tests/data/fonts/`, used only by the tests; not shipped |

## Code and data derived from other projects

**webamp** — https://github.com/captbaritone/webamp — MIT License, Copyright (c) 2015 Jordan Eldredge.
Sprite coordinates and window layouts for the main, equalizer and playlist windows (`src/skins/sprites.h`),
the TEXT.BMP font map (`src/skins/skin.cpp`), `region.txt` and `pledit.txt` parsing rules, window snapping
(`src/ui/snap.cpp`), the EQ graph spline (`src/ui/equalizer_window.cpp`, itself adapted from
[morganherlocker/cubic-spline](https://github.com/morganherlocker/cubic-spline), MIT), the analyzer's bar/peak
behaviour (`src/vis/visualizer.cpp`), Winamp's built-in EQ presets (`src/audio/eq_presets.h`), the windowshade sprites
of `titlebar.bmp`/`eq_ex.bmp`/`pledit.bmp`, the `gen.bmp` frame and bitmap-font layout (`src/ui/gen_window.cpp`, `src/skins/skin.cpp`)
and the `.eqf` file layout (`src/audio/eq_presets.cpp`, after webamp's `winamp-eqf` package) are ported from webamp.

**Yaamp** — https://github.com/Kickoman/yaamp (fork of https://github.com/umnik1/yaamp) — MIT License, Copyright (c) 2025 Maksim Chingin.
Product idea, the set of Yandex Music features and the bundled skin selection come from Yaamp.

**yandex-music-client** — https://github.com/umnik1/yandex-music-client — the track-link signing scheme (`src/yandex/track_url.cpp`)
and the endpoint shapes used in `src/yandex/library.cpp`.

**Milkdrop presets** — `resources/milkdrop/`: 150 presets from the projectM team's
["Cream of the Crop" collection](https://github.com/projectM-visualizer/presets-cream-of-the-crop) (curated by ISOSCELES; authors are named in
the file names). Milkdrop presets were mostly released without a license; the collection treats them as public domain and removes presets on
request, and so does QiYaa. Details in `resources/milkdrop/README.md`.

**yandex-music-api** — https://github.com/MarshalX/yandex-music-api — the unofficial API documentation of that project was used as a reference
for the shapes of the rotor session, wave feedback, landing and playlist-recommendation endpoints. No code was copied.

The equalizer uses the peaking-filter formulas from Robert Bristow-Johnson's *Audio EQ Cookbook* (public knowledge, no code copied).

<details><summary>MIT License text (webamp, Yaamp, miniz)</summary>

```
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
</details>

## Skins

`resources/skins/*.wsz` are freeware Winamp 2.x skins by their respective authors, redistributed as Webamp and Yaamp do.
Each archive keeps its original readme where the author provided one.

| Skin | Author |
|---|---|
| base-2.91 | Winamp base skin as distributed with Webamp |
| Green Dimension V2 | see readme in the archive |
| Mac OS X 1.5 (Aqua) | DeeLight |
| Skinner's Atlas 1.5 | Jellby et al. |
| TopazAmp 1.2 | see readme in the archive |
| Vizor 1.01 | see readme in the archive |
| XMMS Turquoise | XMMS project |
| Zaxon Remake 1.0 | Daniel (see readme) |

"Winamp" is a trademark of its owner. QiYaa is not affiliated with Winamp or Yandex.
