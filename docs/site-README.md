<p align="center">
  <img src="docs/media/banner.jpg" alt="SamNPlayer — Generate · Play · Train" width="900" />
</p>

<h1 align="center">SamNPlayer</h1>

<p align="center">
  <strong>Generate · Play · Train</strong> — local desktop app for
  <code>.samn</code> / <code>.funscript</code> and SVAKOM Sam Neo&nbsp;2 / Neo&nbsp;2&nbsp;Pro
</p>

<p align="center">
  <img alt="Closed source" src="https://img.shields.io/badge/source-private-334155?style=flat-square" />
  <img alt="Downloads" src="https://img.shields.io/badge/downloads-public%20releases-0d9488?style=flat-square" />
  <img alt="No Electron" src="https://img.shields.io/badge/GUI-~13%20MB%20native-0d9488?style=flat-square" />
  <img alt="Local only" src="https://img.shields.io/badge/AI-local%20optional-334155?style=flat-square" />
</p>

<p align="center">
  <a href="https://github.com/funfunpayer/SamNPlayer-site/releases/latest"><strong>Download portable</strong></a>
  ·
  <a href="./index.html">Product page</a>
  ·
  <a href="#why-samnplayer">Why this app</a>
  ·
  <a href="#screenshots">Screenshots</a>
</p>

> **Public:** this page, the [HTML landing](./index.html), and **release binaries**.  
> **Private:** application source (team only) — not in this repository.

---

## Why SamNPlayer

| | Typical alternatives | **SamNPlayer** |
|---|---|---|
| App size | Electron often 100+ MB | **~13 MB** native GUI |
| Scope | Play *or* generate | **Play + generate + device + training** |
| AI | Cloud / AI-only scripts | **Local optional ONNX** — AI proposes, classical tracking writes scripts |
| Privacy | Telemetry common | **Local-only** |
| Video tools | Install ffmpeg yourself | **Portable zip** ships ffmpeg next to the app |
| Device | Generic clients | **Sam Neo 2 first-class** |
| Quality | “Looks fine” | **Quality Doctor** / measured checks |

---

## Screenshots

<p align="center">
  <img src="docs/media/gui-generator.png" alt="Generator" width="900" /><br />
  <em>Generate</em> — mark ROI(s), Tf/Tj, Quality Doctor on the rail
</p>

<p align="center">
  <img src="docs/media/gui-playback.png" alt="Playback" width="900" /><br />
  <em>Playback</em> — video sync or script-alone; heatmap, contact vibration
</p>

<p align="center">
  <img src="docs/media/gui-player-rail.png" alt="Player meters" width="900" /><br />
  Live device meters, soft, Extended-O
</p>

---

## Downloads (public)

Windows and Linux **GUI + CLI** builds (prefer **portable** — ffmpeg included)
are attached to releases on **this** repository:

**→ [Latest release](https://github.com/funfunpayer/SamNPlayer-site/releases/latest)**

Verify with `checksums.txt` in the same release.

| Platform | Now | Later |
|----------|-----|-------|
| Windows / Linux | GUI + CLI + portable ffmpeg | — |
| macOS | — | Signed `.app` when a Mac builder exists |
| Phone | — | Player only |

Personal license: **€40 / year** (see [product page](./index.html#license)). Enforcement rolls out after checkout is live.

---

## Source

The application repository is **private**. There is no public fork or clone of
the product source. This showcase ships binaries, screenshots, and the landing
page only.

© 2026 funfunpayer
