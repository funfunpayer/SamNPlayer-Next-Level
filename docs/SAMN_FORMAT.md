# SamNPlayer native script (`.samn`)

**Source of truth for Sam Neo 2 work.** Community `.funscript` stays an
**import/export** format (like FunGen: rich project in, Funscript out).

Do not confuse with the OFS-style session sidecar `*.snp.json` (offset,
loop, last seek) — that remains a lightweight project file beside media.

## Why a native format

| Need | In `.funscript` alone | In `.samn` |
|------|------------------------|------------|
| One stroke for Handy / Launch / share | `actions` | export only |
| Separate vibration + suction | awkward metadata | first-class curves |
| Chapters / bookmarks | already in metadata | first-class |
| Recipe vs explicit axes | possible | explicit `playbackSource` |
| Strength presets (soft / normal / strong) | live-only today | stored presets |
| Tracking gaps, O-markers, quality | scattered metadata | one document |

Stuffing everything into Funscript `metadata` risks other tools rewriting
the file and dropping SamN fields. Native file = clear ownership; export =
compatible stroke.

## File shape (version 1)

```json
{
  "version": 1,
  "kind": "samnplayer.script",
  "creator": "SamNPlayer",
  "durationMs": 120000,
  "videoPath": "clip.mp4",
  "profile": "tj",
  "playbackSource": "axes",
  "recipe": {
    "sync": "suction_position",
    "min_suction": 0.2,
    "tick_ms": 50,
    "max_speed": 0.5,
    "smoothing": 0.22,
    "contact_vibration": true,
    "contact_vibration_span": 0.75,
    "contact_vibration_curve": "soft"
  },
  "general": [{"at": 0, "pos": 20}, {"at": 1000, "pos": 90}],
  "vibration": [{"at": 0, "pos": 0}, {"at": 1000, "pos": 70}],
  "suction": [{"at": 0, "pos": 20}, {"at": 1000, "pos": 90}],
  "chapters": [{"name": "Intro", "startTime": 0, "endTime": 15000}],
  "bookmarks": [{"name": "Peak", "time": 60000}],
  "oMarkers": [
    {"startMs": 55000, "endMs": 62000, "kind": "primary", "intensity": 1.0},
    {"startMs": 20000, "endMs": 28000, "kind": "secondary", "intensity": 0.45}
  ],
  "trackingGaps": [{"start_ms": 8000, "end_ms": 8200}],
  "strengthPresets": [
    {"name": "soft", "vibrationScale": 0.7, "suctionScale": 0.85, "contactSpan": 0.85, "contactCurve": "soft"},
    {"name": "normal", "vibrationScale": 1.0, "suctionScale": 1.0},
    {"name": "strong", "vibrationScale": 1.3, "suctionScale": 1.1, "contactSpan": 0.55, "contactCurve": "peak"}
  ],
  "activeStrength": "normal"
}
```

### Curves (0–100)

| Field | Role |
|-------|------|
| `general` | Stroke / community axis (required). Exported as Funscript `actions`. |
| `vibration` | Neo 2 vibe intensity (optional). |
| `suction` | Neo 2 suction intensity (optional). |

### `playbackSource`

| Value | Player behaviour |
|-------|------------------|
| `recipe` (default) | Derive vibe/suction from `general` via `recipe.sync` (incl. contact vibe). |
| `axes` | Drive vibe/suction from `vibration` / `suction` curves. Missing channel → 0. |

### Contact vibration

Stored on `recipe` (`contact_vibration`, `_span`, `_curve`). In **recipe**
mode the mapper builds vibe from proximity; in **axes** mode “Bake axes”
(or saving contact settings) regenerates the `vibration` curve from those
fields. Strength presets may override span/curve at playback.

### O-markers

First-class `oMarkers` (primary / secondary climax ranges) live in `.samn`
and are copied into Funscript `metadata.oMarkers` on export. Extended-O
sidecar `*.marker.json` remains a separate live/auto trigger — additive.

### Strength presets

Named scales applied at **playback** (do not rewrite curves). Useful when
the same script should feel softer/stronger without a second file.
`activeStrength` selects which preset is default in the UI.

## Workflows

```text
  Generate (Tf/Tj + contact)
        │
        ├─► clip.samn     (native: general + baked vib/suc when Go pipeline)
        └─► clip.funscript (export: actions = general only)

  Open clip.funscript  ─if clip.samn exists─► prefer .samn
  Open clip.samn       ─────────────────────► Document as written

  “Export Funscript”   ────────► actions + chapters/bookmarks/oMarkers metadata
```

## When not to use three curves

Standard one-axis scripts: only `general` + `playbackSource: recipe`.
Do not duplicate the same points into vibration/suction “for completeness”.

Bake vib/suc when Tf/Tj (and contact) produces **distinct** signals, or
when the user edits a Neo-2 channel in the editor.

## AI / training

AI ROI training still **never writes** scripts (see `docs/KI_TRAINING.md`).
The generator (classic / native pipeline) writes `.samn` + `.funscript`.

### `sceneMap` (optional, M4)

When Generate runs with RhythmGrid and produces a map, the companion `.samn`
may include `sceneMap`:

| Field | Role |
|-------|------|
| `version` | schema version (1) |
| `video` | `durationMs`, `width`, `height`, `sha256_head` (first MiB) |
| `grid` | `cols` / `rows` (rhythm cells) |
| `windows[]` | `startMs`/`endMs`, `tempoHz`, `score_b64` (std base64 of `[]uint8`), `chosen`, optional `signRule` / `trackerR` / `box` / `marks` ids |
| `marks[]` | exclude / source / region (`id`, `kind`, `rect`, time scope, `author`, class/role/confidence/reviewed as applicable) |
| `events[]` | optional engine incidents (may be empty) |

Additive and optional — old readers ignore it. **Community `.funscript` export
never carries `scene_map` / `sceneMap`** (size). `contactMarks` and
`trajectory` are unchanged.

## Compatibility

- Older SamNPlayer: opens `.funscript` as today.
- New SamNPlayer: prefers `.samn` when present; Funscript import remains.
- Session sidecar `*.snp.json` unchanged.
