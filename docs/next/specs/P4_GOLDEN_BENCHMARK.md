# P4 — Golden-clip manifest v2 and benchmark report

**Status:** spec, not built · input to ENG-01 (gate N2) · 12 Oct 2026

## Why

Every Next claim ("AutoDirector is better", "the Rust port is faster") needs the
same clips, the same hardware and a report that can be compared run to run.
Free already has the pieces; they are not enough to gate Next:

| Free today | Gap for Next |
|---|---|
| `generator/golden_clip_benchmark.py` + manifest `{clips:[{name, video, roi, roi2?, profile, backend, reference_funscript}]}` | Absolute paths, no clip hash, no tags, no expected-outcome per clip |
| Result per clip: `ok`, `error`, `quality_score`, `quality_passed`, `correlation.r` | No time, RAM or throughput; no lost frames; no per-scene data |
| History as JSONL, `git_commit`, timestamp | No machine fingerprint; runs on different PCs are mixed |
| `funscript.ScorePair` labels `good / review / bad` | Kept as is |

## Rules

1. **Videos never enter git.** The manifest names clips by content hash; the
   operator maps hashes to local files.
2. **Manifest v1 stays readable.** The v2 reader accepts v1 files unchanged.
3. **A gate compares two reports from the same machine fingerprint.** Reports
   from different machines are shown side by side, never as a pass/fail.
4. **No quality number without a reference.** A clip without
   `reference_funscript` gets Quality Doctor only, and the report says so.

## Manifest v2

Lives in git (`benchmarks/manifests/*.json`). Paths are relative to a clip root
set per machine (`SAMN_GOLDEN_ROOT`), never absolute.

```json
{
  "version": 2,
  "kind": "samnplayer.golden-manifest",
  "clips": [
    {
      "id": "hub-easy-01",
      "file": "hub-easy-01.mp4",
      "sha256": "<full-file sha256>",
      "tags": ["hub", "cfr30", "1080p"],
      "roi": [100, 80, 120, 160],
      "roi2": null,
      "profile": "standard",
      "backend": "csrt",
      "reference_funscript": "refs/hub-easy-01.funscript",
      "expect": { "min_r": 0.55, "quality_passed": true }
    }
  ]
}
```

- `id` — stable name; used in reports and history.
- `sha256` — the runner refuses a clip whose hash differs (a re-cut clip is a new id).
- `tags` — the bake-off matrix: `hub`, `two-body`, `cut`, `camera-move`, `vfr`,
  `no-audio`, `long-60min`, `cfr30`, `cfr60`, `1080p`, `720p`, `vr`.
- `expect` — optional per-clip floor. A run below it fails the gate even if the
  average improves.
- Reference funscripts may be committed (they contain no video), as Free does
  under `generator/testdata/golden_clips/`.

**Minimum set before ENG-02:** the three clips from Free `docs/GOLDEN_CLIPS.md`
(easy hub, two-body, cut/camera move) plus one VFR clip and one clip without
audio. More clips are welcome; fewer block N2.

## Report v2

One JSON per run, `benchmarks/results/<date>-<machine>-<commit>.json`. Results
in git contain only numbers and ids.

```json
{
  "version": 2,
  "kind": "samnplayer.golden-report",
  "started_at": "2026-11-02T10:00:00Z",
  "git_commit": "abc1234",
  "pipeline": { "name": "baseline-python-csrt", "version": "0.6.0" },
  "machine": {
    "fingerprint": "sha256 of os+cpu+gpu+ram, short",
    "os": "windows-11-23h2", "cpu": "…", "gpu": "…", "ram_gb": 32
  },
  "runs": { "warm": 3, "measured": 5 },
  "clips": [
    {
      "id": "hub-easy-01",
      "ok": true,
      "error": null,
      "wall_ms":   { "median": 41200, "p95": 43900 },
      "peak_rss_mb": { "median": 812, "p95": 840 },
      "frames_per_s": 58.1,
      "lost_frames": 12,
      "quality": { "score": 0.81, "passed": true, "flags": ["gap_over_500ms"] },
      "reference": { "r": 0.612, "lag_ms": 40, "polarity": "normal", "label": "good" },
      "scenes": [ { "start_ms": 0, "end_ms": 18000, "lost_frames": 0, "r": 0.70 } ]
    }
  ],
  "summary": { "ok": 5, "total": 5, "quality_passed": 4, "mean_r": 0.58 }
}
```

`scenes` is empty until the pipeline reports scenes (ENG-02). `reference` is
`null` without a reference script.

## Gate rule (compare candidate C against baseline B)

On the same machine fingerprint and manifest:

1. Every clip that was `ok` in B is `ok` in C.
2. No clip falls below its `expect` floor.
3. Quality: **no regression beyond measured noise.** Noise = the spread of
   `r` over the 5 measured runs of B per clip. No clip's median `r` may drop by
   more than that spread, `mean_r(C) ≥ mean_r(B)` within the summed noise, and
   the Quality Doctor `passed` count does not drop. This matches the inherited
   self-build rule (`docs/SELF_BUILD.md`: worse in any measured point → reject).
   Until the first baseline measures the noise, the tolerance is **0**.
4. Speed claim (only when the PR claims speed): median `wall_ms` or `peak_rss_mb`
   at least 20 % better summed over all clips (`FREE_TO_STUDIO_PRODUCT_PLAN` §4).
   Thresholds are revised once the first baseline exists.

The runner exits `0` on pass, `1` on fail, `2` on bad input (hash mismatch,
missing file), so CI and scripts can use it.

## Tests (L1, no real video)

- v1 manifest parses into v2 with defaults.
- Hash mismatch → exit 2, clip named in the error.
- Gate rule against hand-made report pairs: pass, quality drop, single-clip drop,
  `ok → failed`, mixed fingerprints (refused).
- A synthetic 5-second clip generated by ffmpeg (`testsrc`) runs end to end in CI.
