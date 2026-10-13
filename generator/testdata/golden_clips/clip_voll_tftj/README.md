# Golden clip: `clip_voll` (Tf/Tj)

First real (non-synthetic) Tf/Tj comparison dataset, supplied by the user
21 Sep 2026. Used for `docs/NEXT.md` "First real Tf/Tj golden-clip
measurement" and `docs/FINDINGS_TIMING_TF.md`. Per `docs/GOLDEN_CLIPS.md`,
the source video itself is **not** committed (license + size); only the
`.funscript`/`.samn` outputs, which are small text/JSON.

## Contents

Two sub-directories, one per FunGen reference, each laid out for
`fungen_compare.py --dataset DIR` (reference `.funscript` + SamNPlayer
`*__hub.funscript`/`*__tj.funscript` batch-output naming convention):

- `mit_yolo/` — FunGen 2.6.3 reference generated with FunGen's own YOLO
  detector, plus SamNPlayer's `tj`-profile AND `hub`-profile (no Tf/Tj,
  single ROI) output for the same clip.
- `ohne_yolo/` — FunGen 2.6.3 reference generated with YOLO disabled
  (classical detector only — the fairer comparison against SamNPlayer's
  own classical CV tracking), plus the same SamNPlayer `tj`/`hub` output.
- `clip_voll.samn` — SamNPlayer's native `.samn` sidecar for the `tj` run
  (provenance only; comparisons use the `.funscript`).

Each `*__tj.funscript` pair (`mit_yolo`/`ohne_yolo`) is a byte-identical
copy of the same SamNPlayer `tj` run; each `*__hub.funscript` pair is
likewise one SamNPlayer `hub` run compared against two references. They
are kept in **separate** sub-directories rather than one shared dataset
directory: `fungen_compare.py`'s `dedupe_by_content()` collapses
identical-content variant files to guard against counting one copied file
twice (its bug-1 fix), which is correct in general but would silently
drop one of these legitimate comparisons if both lived in one directory.
Run each sub-directory separately, do not run `--dataset clip_voll_tftj`
on the parent.

Source clip: `clip_voll.mp4`, ~280s (stays local — see `docs/GOLDEN_CLIPS.md`).
Durations: 277.1s (`ohne_yolo` ref) / 279.999s (`mit_yolo` ref) / 280.1s
(SamNPlayer) — within a few seconds of each other, not a confound here.

## How to reproduce the numbers in `docs/NEXT.md`

```bash
# Whole-clip (existing tool)
python3 generator/fungen_compare.py --dataset generator/testdata/golden_clips/clip_voll_tftj/mit_yolo --max-lag-ms 3000
python3 generator/fungen_compare.py --dataset generator/testdata/golden_clips/clip_voll_tftj/ohne_yolo --max-lag-ms 3000

# Windowed (new tool, generator/fungen_compare_windowed.py)
python3 generator/fungen_compare_windowed.py \
  "generator/testdata/golden_clips/clip_voll_tftj/mit_yolo/clip_voll_2 mit yolo.funscript" \
  "generator/testdata/golden_clips/clip_voll_tftj/mit_yolo/clip_voll_2 mit yolo__tj.funscript" \
  --window-ms 30000 --max-lag-ms 3000
```

Expect: whole-clip r≈0.06 (both references, both profiles), windowed mean
r≈0.22–0.35 with lag drifting roughly -2600..+2800ms across the clip and
orientation flipping partway through (more often for `hub` than `tj` -
see below). Exact per-window numbers can differ slightly run-to-run of
the windowed script depending on window boundary alignment; the
qualitative finding (drift, not noise) is what to check.

## `hub` vs `tj` (added same day, second SamNPlayer run supplied by the user)

The user also supplied a `hub`-profile run (single ROI, no Tf/Tj) for the
same clip, specifically to check whether Tf/Tj mode itself was the
problem. It is not: `hub`'s windowed mean r (0.22 mit_yolo / 0.26
ohne_yolo) is **not better** than `tj`'s (0.35 / 0.27), despite `hub`
having a much higher Quality Doctor score (0.90 vs. `tj`'s 0.57) and far
fewer quality warnings. `hub` also flips orientation far more often
(nearly every window) than `tj`'s one clean flip - consistent with `hub`
sitting closer to zero real correlation, where orientation is close to a
coin flip each window rather than a genuine mid-clip event.

This is a direct, real-clip instance of `docs/SIGNAL_VS_FIDELITY.md`
("Signal Quality ≠ Motion Fidelity"): the higher Quality Doctor score
does not mean `hub` tracks the reference better. Whatever is driving the
low Motion Fidelity here (timing drift, ROI/ground-truth semantics) is
not specific to Tf/Tj mode.
