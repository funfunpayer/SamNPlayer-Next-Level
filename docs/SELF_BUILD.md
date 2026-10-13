# Lean self-build

Fewer dependencies are better for the product — **only if quality does
not drop in any point**. A self-built piece that is slower, less accurate,
or less reliable than what we already ship is **not an option**.

## Gate (must pass before merge)

Ship a self-build **only** when clip/tests show one of:

| Outcome vs current path | Decision |
|-------------------------|----------|
| **Better** (quality and/or speed, no regression elsewhere) | Ship |
| **Equal** on the fixed checks / golden clips | Ship (dependency win) |
| **Worse** in any measured point | **Reject** — keep the existing path |

Always compare on a real or synthetic **clip** (same inputs as today).
No “looks fine” without numbers. Same bar as `docs/ROADMAP.md`
principles 2, 6, 7 and `CONTRIBUTING.md`.

**Fallback-only helpers** (e.g. lean MP4 probe when ffprobe is missing)
must not replace the better path when that path is available. Prefer
ffprobe/ffmpeg/OpenCV when present; lean code is the spare tire, not a
silent downgrade.

## Rule of thumb

1. Open source shows *how* — we build the **smallest** module that covers
   *our* use case.  
2. Full reimplementation of codecs / browsers / BLE stacks stays out —
   keep thin kernels.  
3. Never vendor a heavy stack “because FunGen has it” without a golden
   win.  
4. Never ship a self-build that makes the product feel verbaut
   (bloated, locked, or weaker).

## Done (self-built / lean) — all gated

| Area | Instead of | Ours | Gate |
|------|------------|------|------|
| Post-track signal | Python scipy | `posttrack` | Python goldens |
| NCC tracking | Python OpenCV | `simpletrack` | e2e clips |
| Audio tempo | Python-only | `audiocheck.go` | synthetic + optional clip |
| ISO BMFF probe | ffprobe always | lean fallback | **equal geometry/duration vs ffprobe on clip**; ffprobe still preferred |
| ffmpeg discovery | system-only install | portable / tools dir | resolve tests |

## Keep as thin kernels

| Kernel | Why |
|--------|-----|
| **ffmpeg** decode/encode | Codec zoo; portable binaries |
| **OpenCV CSRT** | Quality-critical; no equal Go CSRT yet |
| **OS webview** | Avoid Electron bulk |
| **Platform BLE / Intiface** | OS / vendor |

## Next candidates (clip gate required)

| Candidate | Must match or beat |
|-----------|-------------------|
| Go `auto_roi` | Python ROI on golden clips |
| Go flow / `grid_lk` | Python backend Quality Doctor + FunGen r |
| More container probes | ffprobe on same files; no wrong playable hints |

See also: `docs/ENGINE.md`, `docs/GOLDEN_CLIPS.md`, `docs/FFMPEG_TOOLS.md`.
