# Audio in the generate workflow

**Roadmap:** part of classical Generate phase **G1**
(`docs/PRODUCTION_ROADMAP.md`, `docs/GENERATE_HEURISTICS.md`). Stabilize
CSRT (G0) first; do not invent positions from audio; AI comes later (G3).

**Status today:** still shipped. Classical tempo check **plus** Speech-Hold /
segment taxonomy hints — not AI, not a motion replacement. Native Go path
runs the same check post-hoc (`CheckAudioTempo`) — no longer forces Python.

## What exists

| Piece | Role |
|-------|------|
| `generator/audiocheck.go` | Go port: compare script stroke tempo (Hz) to audio-energy envelope tempo |
| `generator/audio_segments.go` | Same post-hoc path: Speech-Hold + segment labels (`holding` / `gentle` / `intense` / `climax`) as review/chapter hints |
| `generator/audio_check.py` | Python equivalent (PreferPython / non-native backends) |
| CLI `--audio-check` | Opt-in |
| GUI Advanced → “Check script tempo against audio” | *(removed from Advanced)* |
| GUI Review → Improve → Audio check | Opt-in toggle (default **on** when ffmpeg is on PATH); also drives fill-gap spacing |
| Result | Warnings + optional `segments` / `speech_hold_ms` in report / funscript `metadata.audio_check` — never changes Quality Doctor pass/score or actions |

## What it is *not*

- Not “listen to the soundtrack and invent a funscript.”
- Not a substitute when tracking finds no motion — audio alone has no
  reliable 0–100 position curve for Neo 2.
- Not Silero/PANNs motion generation (funscript-ai-style stroke writer).
  Speech-Hold and segment labels are **taxonomy / review hints** only.

## Best place in the workflow (recommendation)

```text
  1. ROI / profile (as now) — second-pass VerifyROI warns if the box looks weak
  2. Track video → build general curve (Go CSRT/simpletrack when eligible)
  3. Quality Doctor (Go dense on native path)
  4. Audio tempo check (when ffmpeg available)  ← post-hoc, same path
  5. Write .samn + .funscript
```

**Why after tracking, not before:** the useful comparison is
*script Hz vs audio Hz*. Without a script there is nothing to validate.
A pre-pass can only estimate a *tempo hint*, not the stroke shape.

### Speech-Hold + segment labels (shipped, review-only)

Classical multi-band short-time energy on the same decoded mono samples as
the tempo check (impact ~50–250 Hz, speech ~250–2000 Hz, high ~2–4 kHz).
No ML VAD/tagger dependency.

| Output | Meaning |
|--------|---------|
| `segments[].label` | `holding` · `gentle` · `intense` · `climax` — chapter/Feel taxonomy beside Slibowitz Insertion/Grind/Finish |
| `segments[].speech_hold` | Dialogue/quiet mid-band dominance → prefer hold/pause in review |
| `speech_hold_ms` | Total ms tagged speech-hold |
| Extra warnings | Speech-hold duration; script motion *inside* speech-hold windows; climax chapter cue |

Still never rewrites the curve. Everyday Go-CSRT remains the stroke author.

**GUI (Create → Review):** when Audio check runs, the Review panel shows a
Speech-Hold / Feel segment strip (filter labels, speech-hold-only, click to
seek Play, optional add-visible-as-chapters). Warnings still appear in the
Quality box. Metadata remains the source of truth for CLI / files.

**GUI (Play):** `LoadFunscript` exposes `speechHoldMs` + `audioSegments` from
`metadata.audio_check` (and the companion `.funscript` when loading `.samn`).
Play shows the same strip — filters, click-to-seek, optional chapters — plus
filtered translucent bands on the heatmap (thin curve underlay; “Show on
heatmap” toggle). Display only — so a saved script can be reviewed without
re-running Create Audio check.

### Optional later enhancements (not built)

| Idea | Fit | Notes |
|------|-----|--------|
| **GUI chapter list from `segments`** | **Shipped (Review + Play)** | Create Review + Play strips: filters · seek · optional chapter marks from visible segments |
| **Default-on post-check** | Yes | Already GUI default when ffmpeg present |
| **Pre-pass tempo hint** | Later | Estimate audio Hz first → bias `min_peak_distance` / smooth; still track video for shape. Stroke preview (Stage A) may supply a *video* tempo hint without audio. |
| **“No motion → fall back to audio”** | Weak | Can flag “retry ROI / wrong axis”; must not invent a full position script from loudness. Prefer `strokepreview.Report.SuggestAudio` after weak/unstable preview. |

### Recommended product rule

1. **Always run post audio-check** when ffmpeg is available (GUI default on).
2. If tracking span is tiny / Quality Doctor fails *and* audio tempo is
   clear: surface a hint (“audio suggests ~X Hz — check ROI / axis”),
   do not auto-write actions from audio.
3. Defer a real pre-pass until golden-clip numbers show tempo hints help
   more than they hurt (principle 6).

## Relation to `.samn`

Warnings can live in funscript metadata today; when baking `.samn`,
copy `audio_check` into the native doc as informational quality
sidecar (same as `qualityScore`) — optional follow-up, not required for
the check itself.
