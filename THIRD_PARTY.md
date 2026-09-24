# Third-party components

QiYaa itself is MIT-licensed (see `LICENSE`). It includes or links the following.

## Linked libraries

| Component | License | How it is used |
|---|---|---|
| [Qt 6](https://www.qt.io/) — Core, Gui, Widgets, Network, OpenGL, DBus (Linux) | LGPLv3 | Linked **dynamically**. You may replace the Qt libraries shipped with a build by your own. Qt sources: https://download.qt.io/official_releases/qt/ |
| [projectM](https://github.com/projectM-visualizer/projectm) 4.1.7 — the Milkdrop engine | LGPL-2.1 | Linked **dynamically** (`libprojectM-4.so` / `projectM-4.dll` / `.dylib`, shipped next to the app); replaceable by your own build of the same version. Sources: https://github.com/projectM-visualizer/projectm/tree/v4.1.7 |

Only LGPL-licensed Qt modules are used. GPL-only modules (Qt HTTP Server, Qt Network Authorization, etc.) are deliberately not used.

projectM's library contains its vendored components: [projectm-eval](https://github.com/projectM-visualizer/projectm-eval) (MIT),
[hlslparser](https://github.com/Thekla/hlslparser) (MIT), [SOIL2](https://github.com/SpartanJ/SOIL2) (MIT-0; with Apache-2.0 ETC1 and MIT PowerVR code)
and [GLM](https://github.com/g-truc/glm) (MIT). On Windows it uses [GLEW](https://github.com/nigels-com/glew) (Modified BSD / MIT), shipped as `glew32.dll`.

## Bundled source code

| Component | Version | License | Location |
|---|---|---|---|
| [miniaudio](https://github.com/mackron/miniaudio) — David Reid | 0.11.25 (`9634bed`) | Public domain / MIT-0 | `third_party/miniaudio/` |
| [miniz](https://github.com/richgel999/miniz) — Rich Geldreich, RAD Game Tools, Valve | 3.1.2 (`77d0dce`) | MIT | `third_party/miniz/` |

## Code and data derived from other projects

**webamp** — https://github.com/captbaritone/webamp — MIT License, Copyright (c) 2015 Jordan Eldredge.
Sprite coordinates and window layouts for the main, equalizer and playlist windows (`src/skin/SkinSprites.h`),
the TEXT.BMP font map (`src/skin/Skin.cpp`), `region.txt` and `pledit.txt` parsing rules, window snapping
(`src/ui/Snap.cpp`), the EQ graph spline (`src/ui/EqualizerWindow.cpp`, itself adapted from
[morganherlocker/cubic-spline](https://github.com/morganherlocker/cubic-spline), MIT), the analyzer's bar/peak
behaviour (`src/vis/Visualizers.cpp`), Winamp's built-in EQ presets (`src/audio/EqPresets.h`), the windowshade sprites
of `titlebar.bmp`/`eq_ex.bmp`/`pledit.bmp`, the `gen.bmp` frame and bitmap-font layout (`src/ui/GenWindow.cpp`, `src/skin/Skin.cpp`)
and the `.eqf` file layout (`src/audio/EqPresets.cpp`, after webamp's `winamp-eqf` package) are ported from webamp.

**Yaamp** — https://github.com/Kickoman/yaamp (fork of https://github.com/umnik1/yaamp) — MIT License, Copyright (c) 2025 Maksim Chingin.
Product idea, the set of Yandex Music features and the bundled skin selection come from Yaamp.

**yandex-music-client** — https://github.com/umnik1/yandex-music-client — the track-link signing scheme (`src/yandex/TrackUrl.cpp`)
and the endpoint shapes used in `src/yandex/Library.cpp`.

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
