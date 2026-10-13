# Signal Quality ≠ Motion Fidelity

Canonical distinction (code review / project summary, 17 September 2026).
Must stay visible in GUI copy, reports, and API field names — not only in
planning docs.

## Signal Quality

**Question:** Is the funscript *technically plausible as a control signal*?

Examples: jitter, impossible jumps, continuity, tracking-loss fraction,
smoothing artefacts, out-of-range values, device-compat density.

**Tools today:** Quality Doctor (`quality_doctor.py`), Script Doctor
(`funscript.EvaluateScriptQuality` / playback “Skript prüfen”),
device-compat checks.

A high Signal Quality score does **not** prove the curve matches the video.

## Motion Fidelity

**Question:** Does the signal *faithfully represent the motion in the video*
(or a FunGen / human reference)?

Examples: rhythm, phase / `best_lag_ms`, turning-point timing, amplitude,
speed profile, event timing, reference correlation (raw vs aligned).

**Tools today:** `fungen_compare.best_lag_correlation`,
`funscript.BestLagCorrelation` / `DiagnosePhase`, CLI
`SamNPlayer-cli phase A B`, Golden-Clip Benchmark correlation column.

A high Motion Fidelity score can coexist with mediocre Signal Quality
(noisy but shape-correct) and vice versa (smooth but wrong phase/ROI).

## Product rule

| Surface | Must report |
|---|---|
| Script Doctor / post-generate Quality Doctor | **Signal Quality** (label it) |
| FunGen / reference compare, Phase CLI, Golden-Clip `correlation` | **Motion Fidelity** (label it) |
| Future fusion / SAM Perception reports | Both, never one number that mixes them |

Never market or UI-copy a Quality Doctor score as “matches the video”.

## Next milestone this distinction enables

**SAM Perception v1** (see `docs/SAM_ARCHITECTURE.md`): measure multiple
existing observers on the same golden segments, score Signal Quality and
Motion Fidelity separately, then fuse by confidence — not “add another
tracker to the dropdown”.
