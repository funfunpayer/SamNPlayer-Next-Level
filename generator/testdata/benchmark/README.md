# Benchmark fixtures (KI-ready labels)

Everyday Go CSRT remains the production basis. These fixtures help score
Everyday (or hybrid-assisted) candidates against FunGen references and
emit labels AI training can consume later.

Videos stay local (see `docs/GOLDEN_CLIPS.md`). Committed funscripts under
`golden_clips/` are enough for the **compare-only** path.

**Prep short clips:** `scripts/benchmark-prep/` cuts + downscales from
in/out marks (default 1280-wide ≈ 720p for Everyday CSRT; optional 960-wide).
Not part of Everyday Create — owner/benchmark path only.

## Layout

| Path | Role |
|------|------|
| `example_score_pair.json` | One ready-to-score FunGen×hub pair (no video) |
| `example_manifest.json` | Manifest shape for `golden_clip_benchmark.py` (paths are placeholders — point at your local clips) |
| `labels.schema.json` | JSON Schema for `--labels-out` / GUI "Save label for KI" JSONL lines |

## CLI (compare only)

```bash
go run ./cmd/cli benchmark \
  --reference generator/testdata/golden_clips/clip_ausschnitt_native/mit_yolo/clip_ausschnitt.funscript \
  --candidate generator/testdata/golden_clips/clip_ausschnitt_native/mit_yolo/clip_ausschnitt__hub.funscript \
  --json --labels-out /tmp/benchmark_pair_labels.jsonl
```

Exit `0` = label `good` (gut). Exit `1` = `review` or `bad`.

## Labels for KI

Each JSONL line: `{ kind, label, passed, video?, reference, candidate, score }`.
`label` ∈ `good` | `review` | `bad`. Hybrid KI drafts are scored the same way —
basis stays Everyday recognition + FunGen ref, never KI-first Everyday.
