# Platforms — desktop now, mobile player later

SamNPlayer is a **desktop** product first (Generate + Play + Train +
Device). A later **phone player** (play + device only, **no generator**)
is planned; this file is the stake in the ground so architecture does not
accidentally couple mobile to the generator stack.

## Matrix (honest)

| Platform | Status | Notes |
|----------|--------|-------|
| **Windows** amd64 | Shipped | GUI + CLI; portable zip includes ffmpeg + MinGW OpenCV DLLs; Go CSRT via `-tags opencv` (v0.5.13+). Without OpenCV tags, Generate falls back to Python CSRT / simpletrack. |
| **Linux** amd64 | Shipped | GUI (WebKitGTK) + CLI; OpenCV CSRT in release tags; portable tar.gz with ffmpeg |
| **macOS** (Intel / Apple Silicon) | **Not built yet** | No Mac CI runner / signing in this repo today. Code is Go+Wails — darwin is intended; needs a macOS builder, notarization, and ffmpeg tools for darwin. See below. |
| **iOS / iPhone** | Later — **player only** | No Generate / AI Train / Bench. Share `player` + `device` + `funscript`/`samn`. |
| **Android** | Later — **player only** | Same scope as iOS. |

## Desktop (keep excellent)

GitHub exterior (README, screenshots, releases) and the in-app layout must
stay **competitive**: clear brand, one job per tab, Play as the default
home, Generate for creators. Do not dilute the desktop GUI into a
phone-shaped compromise.

Checklist: [`docs/COMPETITIVE.md`](COMPETITIVE.md).

Imported community scripts / Neo 2 optimize / future catalog APIs:
[`docs/CONTENT_SOURCES.md`](CONTENT_SOURCES.md).

## Mobile player (later) — scope fence

**In:**

- Open `.samn` / `.funscript`
- Optional local video (or script-alone)
- BLE / vendor transport for Sam Neo 2 (platform APIs)
- Heatmap / curve / O-markers / Extended-O / strength
- Playlist basics

**Out (desktop-only forever for v1 mobile):**

- Generator, ROI drawing, golden-clip bench, AI Train
- ffmpeg encode proxy (phone OS decoders instead)
- Python / OpenCV tracking

**Shared Go packages (do not drag generator into mobile):**

```
player/     playback clock + Extended-O + training loops
device/     transports (BLE / Intiface / mock)
funscript/  parse + quality helpers used at play time
samn/       native Neo 2 document
```

`cmd/gui-wails` and `generator/` stay desktop. A future
`cmd/mobile-player` (or Flutter/Kotlin/Swift UI over the same Go core via
gomobile / FFI) should import only the share surface above.

## macOS path (when hardware exists)

1. `wails build -platform darwin/universal` (or amd64 + arm64) on a Mac runner  
2. Ship portable folder: `SamNPlayer.app` + ffmpeg/ffprobe in `Contents/MacOS` or Tools  
3. Notarize + staple (Apple requirement for distribution)  
4. Add `SamNPlayer-gui-darwin-*` assets to `update.AssetForThisPlatform` (already suffixes by `GOOS`)  
5. Extend `videox.EnsureTools` darwin case (static build) — stubbed when we have a pinned URL

Until then: Linux + Windows remain the measured release pair; darwin code
paths must not regress when touched.

## CPU / GPU / RAM (stability over slogans)

Measured findings (`docs/NEXT.md`): CSRT already multi-threads inside
OpenCV; Python-level dual-tracker parallelization did **not** win
end-to-end; GPU does **not** accelerate CSRT in stock OpenCV builds.
Therefore:

| Resource | Policy |
|----------|--------|
| **CPU** | Go uses all logical CPUs (`GOMAXPROCS`); do not fake extra parallelism that failed bake-offs |
| **GPU** | Python path tries OpenCL automatically (log only); AI Train uses CUDA/DirectML when present; never claim CSRT is GPU-bound |
| **RAM** | Cache dirs under user config; clear-on-exit setting; avoid unbounded frame buffers |
| **Stability** | Prefer cancelable contexts, no surprise downloads, portable ffmpeg, Quality Doctor gates |

“Fully use the machine” means **measured useful work**, not maxing fans.

## Related

- [`docs/FFMPEG_TOOLS.md`](FFMPEG_TOOLS.md) — video tools without system install  
- [`docs/ENGINE.md`](ENGINE.md) — lean engine direction  
- [`docs/COMPETITIVE.md`](COMPETITIVE.md) — GUI / GitHub bar  
