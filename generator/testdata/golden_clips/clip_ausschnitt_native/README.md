# Golden clip: `clip_ausschnitt` (SamNPlayer native Go vs. real FunGen2)

**Correction (21 Sep 2026): this dataset was originally committed (and merged,
#148) with the wrong provenance.** All three of the user's original uploads
carried `metadata.creator: "SamNPlayer generator/native..."` plus a
`native_pipeline` telemetry block (`frames=1199, lost=0,
range_px=105.0539100525142`) - but two of the three (`mit_yolo/`,
`ohne_yolo/` references below) are actually **FunGen 2.6.3's own tracking**,
not SamNPlayer's. Confirmed by a follow-up upload correctly labeled
`creator: "FunGen 2.6.3 (fungen.app)"` whose actions matched the
`ohne_yolo` reference 715/716 exactly (the one difference: one keyframe's
timestamp, not its position, right at a chapter boundary). 
**On-disk fix (24 Sep 2026):** the two FunGen reference files above are
relabeled to `creator: "FunGen 2.6.3 (fungen.app)"` and no longer carry
`native_pipeline` (actions unchanged). Inspecting `SaveAxisActions` /
`.samn` From/Export found no live stamp of SamNPlayer creator onto foreign
scripts — ChatGPT Lane E + Cursor — so the bad labels look like a packaging
mix-up when the dataset was first committed, not a current load/save bug.
Regression tests lock foreign-creator preservation on save.

This originally-published version described the three files as sharing
"the exact same underlying CSRT tracking run" and treated the comparison as
an internal SamNPlayer consistency check. That framing is wrong for two of
the three files. This corrected version has real external FunGen2
references instead - a genuine Motion Fidelity measurement, same
methodology as `clip_voll_tftj/`. Source video not committed (license +
size, per `docs/GOLDEN_CLIPS.md`).

## Contents

Laid out like `clip_voll_tftj/` for `fungen_compare.py --dataset DIR`:

| File | Actions | True source |
|---|---|---|
| `mit_yolo/clip_ausschnitt.funscript` | 71 | **FunGen 2.6.3**, YOLO on |
| `mit_yolo/clip_ausschnitt__hub.funscript` | 145 | SamNPlayer native Go (`trackcv`+`posttrack`), `hub`/standard profile |
| `ohne_yolo/clip_ausschnitt_ohne_yolo.funscript` | 738 | **FunGen 2.6.3**, YOLO off |
| `ohne_yolo/clip_ausschnitt_ohne_yolo__hub.funscript` | 145 | Same SamNPlayer native Go file as above |
| `samnplayer_python_OLD_bonus.funscript` | 175 | SamNPlayer **Python** path (`generate_funscript.py`), earlier session - not part of the FunGen dataset convention, kept for the Python-vs-Go bonus comparison below |

The two `__hub.funscript` copies are byte-identical (same SamNPlayer run,
compared against two FunGen references) - kept in separate sub-directories
for the same `dedupe_by_content()` reason documented in `clip_voll_tftj/README.md`.

## Measurement (reproduce)

```bash
python3 generator/fungen_compare.py --dataset generator/testdata/golden_clips/clip_ausschnitt_native/mit_yolo --max-lag-ms 1000
python3 generator/fungen_compare.py --dataset generator/testdata/golden_clips/clip_ausschnitt_native/ohne_yolo --max-lag-ms 1000

go run ./cmd/cli phase generator/testdata/golden_clips/clip_ausschnitt_native/mit_yolo/clip_ausschnitt.funscript generator/testdata/golden_clips/clip_ausschnitt_native/mit_yolo/clip_ausschnitt__hub.funscript -max-lag-ms 1000 -window-ms 10000
go run ./cmd/cli phase generator/testdata/golden_clips/clip_ausschnitt_native/ohne_yolo/clip_ausschnitt_ohne_yolo.funscript generator/testdata/golden_clips/clip_ausschnitt_native/ohne_yolo/clip_ausschnitt_ohne_yolo__hub.funscript -max-lag-ms 1000 -window-ms 10000
```

**SamNPlayer native Go vs. real FunGen2** (this is a real Motion Fidelity
score, not internal consistency):

| metric | vs. mit_yolo | vs. ohne_yolo |
|---|---|---|
| whole-clip r | 0.273 (lag -800ms, inverted) | 0.440 (lag 0ms) |
| windowed (10s) mean r | 0.533 | 0.590 |
| per-window lag range | +0..+700ms | -900..+0ms |
| orientation | flips (3 normal / 2 inverted) | flips (2 normal / 3 inverted) |

Same drift signature as `clip_voll_tftj/` (see `docs/FINDINGS_TIMING_TF.md`
F-003): whole-clip r looks weak, windowed r rises substantially, lag isn't
constant, orientation flips mid-clip.

**Bonus: Python path vs. the same FunGen2 references**, same clip, same
methodology:

| metric | Python vs. mit_yolo | Python vs. ohne_yolo |
|---|---|---|
| whole-clip r | 0.236 (inverted) | 0.165 (inverted) |

Native Go's whole-clip r (0.27 / 0.44) is comparable-to-better than the
older Python path's (0.24 / 0.17) against the *same* real FunGen2
references on this clip - the Go migration is not a regression here, but
neither path is clean; F-003 affects both.

## Open item

No independent confirmation yet that F-003 (frame-index timestamp drift)
is *specifically* a `posttrack` bug rather than a cross-tool ROI/timing
convention difference - this dataset alone can't separate the two, same
limit as `clip_voll_tftj/`. Would need two SamNPlayer exports confirmed to
share one real tracking run (with correct, non-buggy provenance metadata)
to isolate that, which this dataset does not currently provide.
