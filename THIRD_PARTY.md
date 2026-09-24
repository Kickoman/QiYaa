# Third-party components

QiYaa itself is MIT-licensed (see `LICENSE`). It includes or links the following.

## Linked libraries

| Component | License | How it is used |
|---|---|---|
| [Qt 6](https://www.qt.io/) — Core, Gui, Widgets, Network | LGPLv3 | Linked **dynamically**. You may replace the Qt libraries shipped with a build by your own. Qt sources: https://download.qt.io/official_releases/qt/ |

Only LGPL-licensed Qt modules are used. GPL-only modules (Qt HTTP Server, Qt Network Authorization, etc.) are deliberately not used.

## Bundled source code

| Component | Version | License | Location |
|---|---|---|---|
| [miniaudio](https://github.com/mackron/miniaudio) — David Reid | 0.11.25 (`9634bed`) | Public domain / MIT-0 | `third_party/miniaudio/` |
| [miniz](https://github.com/richgel999/miniz) — Rich Geldreich, RAD Game Tools, Valve | 3.1.2 (`77d0dce`) | MIT | `third_party/miniz/` |

## Code and data derived from other projects

**webamp** — https://github.com/captbaritone/webamp — MIT License, Copyright (c) 2015 Jordan Eldredge.
Sprite coordinates and main-window layout (`src/skin/SkinSprites.h`), the TEXT.BMP font map (`src/skin/Skin.cpp`),
`region.txt` parsing rules (`src/skin/Region.cpp`) and window snapping (`src/ui/Snap.cpp`) are ported from webamp.

**Yaamp** — https://github.com/Kickoman/yaamp (fork of https://github.com/umnik1/yaamp) — MIT License, Copyright (c) 2025 Maksim Chingin.
Product idea, the set of Yandex Music features and the bundled skin selection come from Yaamp.

**yandex-music-client** — https://github.com/umnik1/yandex-music-client — the track-link signing scheme (`src/yandex/TrackUrl.cpp`).

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
