# Golden-Clip protocol

Needed for FunGen parity. This file is the checklist; the clips stay on
the operator machine (license + size).

**Automated as of `generator/golden_clip_benchmark.py`** (GUI: the "Bench"
tab) — point its manifest at clips following this checklist and it runs
them through the real pipeline, computes Quality Doctor + FunGen
agreement per clip, and tracks results over time. This file stays the
checklist for *building* the clip set by hand; the tool is what runs it
repeatably. See `docs/ROADMAP.md` for how this fits the wider task list.

**Compare-only (no generate):** CLI `benchmark` / GUI Bench →
"Compare scripts" scores a candidate `.funscript` against a FunGen
reference (`funscript.ScorePair`, labels `good|review|bad`). Fixture
layout + KI label schema:
`generator/testdata/benchmark/`. Everyday Go CSRT remains the basis;
KI assists in hybrid, it does not replace Everyday.

**Clip prep (owner):** cut + Lanczos-downscale short segments with
`scripts/benchmark-prep/` (default max width **1280** ≈ 720p; alt **960**).
Marks JSON + ffmpeg examples in that folder’s README — not wired into
Everyday Create.

## What one clip must include

1. Source video (same cut FunGen used), note codec and resolution.
2. FunGen export as `.funscript` only — never `.fungen` / project files.
3. ROI1 and ROI2 written once in *video pixels* on frame 0. Reuse them.
4. SamNPlayer outputs: `--profile standard` (hub) and `--profile tj` with
   the same ROIs, CSRT and optionally `grid_lk`.
5. Keep invert *off* for the first pair; if `fungen_compare` reports
   inverted polarity, record that as a separate row, do not hide it in r.

## How to compare

```bash
python3 generator/fungen_compare.py --dataset DIR --output report.md --max-lag-ms 300
```

Report both Quality Doctor and FunGen r. They are not proxies.

## Committed examples

- `generator/testdata/golden_clips/clip_voll_tftj/` — first real Tf/Tj
  dataset (SamNPlayer vs. FunGen references, funscripts only, video stays
  local per above). See its `README.md` and `docs/NEXT.md`/
  `docs/FINDINGS_TIMING_TF.md` for the measurement.
- `generator/testdata/golden_clips/clip_ausschnitt_native/` — native Go
  pipeline (+ Python path, bonus) vs. real FunGen2 references. Corrected
  21 Sep 2026 after a metadata-provenance mix-up (see its `README.md`
  for the full story). See `docs/NEXT.md` / `docs/FINDINGS_TIMING_TF.md`
  (F-003) for the measurement.

## Minimum set

Three clips is enough: one easy hub, one two-body (Tf/Tj), one with a cut
or camera move. More than that without fixed ROIs repeats issue #8.

## Do not change yet

`smooth-window`, peaks, per-scene ROI, upscale, sharpen — only after the
matrix above exists and ROI quality is the remaining variable.

## Tf/Tj + AI two-ROI bake-off (after v0.5.6)

The GUI already offers an **opt-in** two-region suggestion:

- Classic: `auto_two` → `auto_roi.find_two_rois`
- AI: `ai_two` → `ai_roi.find_two_rois` (needs a local ONNX model)

Neither path writes a Funscript by itself — it only fills ROI1/ROI2 for
you to accept or correct. Before promoting either suggestion further:

1. Add at least one **two-body** clip to the golden manifest with
   hand-drawn `roi` + `roi2` and a FunGen `reference_funscript`
   (`docs/GOLDEN_CLIPS.md` minimum set).
2. Run the Bench tab (or `golden_clip_benchmark.py`) twice on that clip:
   - once with the **manual** ROIs from the manifest
   - once after replacing ROIs with what `ai_two` / `auto_two` suggested
     on frame 0 (record the boxes in the history comment / sidecar)
3. Compare Quality Doctor **and** FunGen `r` — suggestion wins only if it
   matches or beats the manual pair on both. Do not default-wire a
   suggestion that only looks plausible in the GUI.

Until those numbers exist, keep AI/auto two-ROI as a suggestion, not a
silent default (issue #8 / principle 6).
