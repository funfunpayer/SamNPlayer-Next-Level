# Findings: Timing, Tf/Tj, Go-native (research triage)

Operator research (16–17 Sep 2026) triaged into this inventory. Direction
summary: `docs/ENGINE.md`. This file records what was **acted on**, what
stays **open pending measurement**, and what is **deferred** — per project
rules: no speculative modules, every quality claim needs a measurement.

## Acted on now

### Tf/Tj suction double-floor (finding §2)

**Claim:** with actions clamped to 20–90 and `MinSuction=0.20`,
`liftFloor(pos/100, 0.20)` remaps resting suction from 0.20 → **0.36**
and peak from 0.90 → **0.92**.

**Verified in code** (`funscript/mapper.go` + `RecipeFor("tj")`): yes.
`TestRecipeTJSuctionNoDoubleFloor` locks the intended mapping:
pos 20 → suction ≈ 0.20, pos 90 → ≈ 0.90.

**Fix:** for `SyncSuctionPosition`, skip the runtime `liftFloor` on
suction. The generation-time 20–90 clamp *is* the floor. Recipe metadata
still reports `min_suction: 0.20` as that script-space floor.

Still needs a **feel check on real hardware** (priority 1) — software
can only assert the command values, not perceived intensity.

### Phase Analyzer core in Go (finding F-001)

**Shipped:** `funscript.BestLagCorrelation` + `DiagnosePhase` — pure-Go
port of `fungen_compare.best_lag_correlation` (shared absolute timeline,
±lag search, orientation, shape error, low-confidence). CLI:
`SamNPlayer phase` / `compare`. Goldens mirror `fungen_compare_test.py`.

### Observation contract (F-004)

**Shipped on trackcv:** `ValidFrames` / `Confidence` / `Reason` (+ cancel)
written into native funscript metadata.

### Dense Quality Doctor + FunGen-compare CLI

**Shipped:** `EvaluateDenseQuality` on native CSRT; `SamNPlayer compare`.

## Signal Quality ≠ Motion Fidelity (17 Sep 2026)

Script Doctor / Quality Doctor = **Signal Quality**. FunGen /
`best_lag_ms` / Phase CLI = **Motion Fidelity**. Canonical:
`docs/SIGNAL_VS_FIDELITY.md`. Next architecture milestone: Perception v1
on real goldens — see `docs/ROADMAP.md` / `docs/ENGINE.md`.

## F-003: FrameIndex/FPS timing drift — **original mechanism refuted, periodicity aliasing confirmed** (21 Sep 2026)

**Original claim (source research material, 16 Sep 2026):** funscript
keyframe timestamps derived from frame *index* × nominal FPS drift from
the video's real presentation timestamps under VFR/frame-rate
imprecision, producing a growing (or otherwise non-constant) offset over
a clip's length rather than one fixed lag.

**The drift signature itself is real, measured on two independent real
clips** (see below), **but its proposed cause (VFR/frame-index drift) is
directly refuted** by a same-day follow-up measurement — see "Root-cause
test" below. The symptom (weak whole-clip r, much higher windowed r,
non-constant per-window lag, orientation flips) is not in question; what
was wrong is *why* it happens.

**Drift signature, measured on two independent real clips:**

1. `clip_voll` (21 Sep 2026, `docs/NEXT.md` "First real Tf/Tj golden-clip
   measurement"): SamNPlayer `tj`/`hub` vs. FunGen references — whole-clip
   r≈0.06, windowed (30s) mean r≈0.22–0.35, per-window lag range up to
   -2600..+2800ms, orientation flip mid-clip.
2. `clip_ausschnitt` (21 Sep 2026, `docs/NEXT.md` "Native Go pipeline vs.
   real FunGen2, `clip_ausschnitt`"): SamNPlayer native Go vs. real
   FunGen2 references (correction same day: originally miscategorized as
   an internal SamNPlayer-only consistency check due to a metadata
   provenance bug — see that `docs/NEXT.md` entry) — whole-clip
   r=0.27–0.44, windowed (10s) mean r=0.53–0.59, per-window lag swings up
   to ±900ms, orientation flips.

**Root-cause test (21 Sep 2026, `clip_ausschnitt`):**

1. Confirmed `generator/trackcv/track.go` computes keyframe timestamps as
   `frame_index × 1000/fps` — exactly the mechanism F-003 describes.
2. Compared that against the video's **real** per-frame presentation
   timestamps (`ffprobe`, `pts_time`) at every sampled frame across the
   entire clip (0 → 1198, start to end).
3. **Result: the discrepancy is a constant 41.02ms at every single
   sampled frame — frame 0, frame 100, frame 600, frame 1198, no
   exceptions, no growth.** `clip_ausschnitt.mp4` is genuine CFR (24fps,
   `r_frame_rate` = `avg_frame_rate` = 24/1); the only error is a fixed
   start-offset, not drift.
4. A **constant** offset is absorbed instantly by any lag search (this
   project's own `±1000ms` default easily covers 41ms) and structurally
   **cannot** produce a lag that swings by up to ±900ms and flips
   orientation across windows. The math doesn't support the mechanism,
   independent of any single measurement.
5. Separately, raw (pre-`posttrack`) tracking output was compared against
   the same FunGen2 references: whole-clip r=0.097 (mit_yolo) / r=0.344
   (ohne_yolo) — **lower** than the already-committed post-processed
   numbers (0.273 / 0.440), not higher. `posttrack`'s smoothing/keyframe
   reduction is not degrading fidelity relative to the raw signal; if
   anything it mildly helps. This also rules out "raw signal is fine,
   `posttrack` corrupts the timing" as an explanation.

**Conclusion: F-003's VFR/frame-index mechanism does not explain the
observed drift on this clip.** The drift signature stays real and
unexplained pending a new hypothesis.

**Alternative hypothesis: periodicity aliasing in the lag search itself
— now CONFIRMED with a synthetic ground-truth test (21 Sep 2026).**
Funscript motion is typically near-periodic (repeated stroke cycles); a
wide lag search (±1000-3000ms here) over a periodic signal can lock onto
a *different cycle* of the same motion that happens to correlate well by
coincidence, rather than the true alignment — producing lag values that
cluster near integer multiples of the dominant stroke period, not real
timing offsets. The initial evidence (dominant period ≈280ms via
autocorrelation on `clip_ausschnitt`'s `ohne_yolo` reference, compared
against the windowed measurement's observed lag values, residuals mostly
within ±0.2-0.5 cycles of an integer multiple) was suggestive but not
conclusive on its own.

**Synthetic ground-truth test (21 Sep 2026,
`funscript.TestPeriodicityAliasingCharacterization` in
`funscript/phase_test.go`, runs in CI, no external fixtures needed):**
built two synthetic signals over a 60s span with a known, constant,
injected 300ms lag (`shifted.At = ref.At + 300`, matching the sign
convention already locked in by `TestBestLagCorrelationShiftedSine`,
where the recovered lag is the negative of the injected offset, i.e.
`-300`):

1. **Periodic** — a 280ms-period sine wave (the same dominant period
   measured on `clip_ausschnitt`).
2. **Non-periodic** — a stroke-like wave alternating low/high, but with
   each half-cycle duration drawn fresh from [130, 450)ms so no cycle
   repeats.

Both pairs are otherwise identical (same amplitude range, same known
lag, same 10s window size). Results:

- **Whole-clip, periodic:** best-lag search reports `r=1.0000` at
  `lag_ms=-1140`, not the true `-300`. `-1140 = -(300 + 3×280)` — the
  search locked onto a candidate exactly 3 full stroke periods away from
  the true offset, because on a perfectly periodic wave every such
  candidate correlates equally perfectly.
- **Whole-clip, non-periodic:** best-lag search reports `r=1.0000` at
  `lag_ms=-300` — the exact true offset, no aliasing possible without a
  repeating cycle to alias onto.
- **Windowed (10s windows), periodic:** lag swings `-1420ms` to
  `+1240ms` across windows, **including one window flipping orientation
  to `inverted`** (a perfectly symmetric triangle/sine stroke shape
  time-shifted by half a period is indistinguishable from its own
  vertical inversion) — despite the true injected offset being exactly
  the same **constant** 300ms in every window, the whole clip through.
  This reproduces, from a known-constant offset and nothing else, the
  same *shape* of pathology seen on the real clips: swinging per-window
  lag and a mid-clip orientation flip.
- **Windowed (10s windows), non-periodic:** every single window reports
  `lag_ms=-300`, `r=1.0000`, `orientation=normal` — exactly the true
  offset, no swing, no flip.

**Conclusion: periodicity aliasing is a real, demonstrated failure mode
of the current lag search on periodic/near-periodic motion, sufficient
on its own (no real drift required) to produce swinging windowed lag and
spurious orientation flips.** This doesn't prove aliasing is the *only*
thing happening on the real clips (real motion isn't perfectly periodic
or noise-free the way this synthetic is), but it proves the mechanism is
real and structurally capable of producing exactly what was observed.
The synthetic test is committed as a permanent characterization test (not
a bugfix — `BestLagCorrelation`/`WindowedBestLagCorrelation` are
unchanged) so this failure mode is documented, reproducible, and can't
silently regress or get "fixed" without anyone noticing.

**What this means going forward:** option **(1) additive AliasingRisk
flag** is shipping (`LagCorrelation.AliasingRisk` /
`AlternateLagsMs` / `DominantPeriodMs`) — reported lag values unchanged.
Option **(2)** actual disambiguation of the lag search remains undecided
and needs its own sign-off plus golden-clip verification before anyone
trusts changed numbers.

**AliasingRisk flag measured against the real golden clips (21 Sep
2026): did not fire on any of them, at any tested resample step —
root cause was NOT a half-period ambiguity, correction below.** Ran
`phase` (whole-clip and windowed) on all four committed real pairs
(`clip_voll_tftj` and `clip_ausschnitt_native`, both `mit_yolo`/
`ohne_yolo`) — every window on every pair reported `aliasing_risk=false`,
despite the same swinging-lag/orientation-flip pattern that motivated
this whole investigation still being clearly present in those numbers
(see the r/lag tables in `docs/NEXT.md`'s bake-off and golden-clip
entries).

The first explanation offered here (same day, in the previous revision
of this paragraph) was that a "half-period ambiguity" — a symmetric
stroke shape time-shifted by half a period looking like its own vertical
inversion — was causing `dominantPeriodMs` to lock onto half of the true
period. **That explanation was wrong.** A closer look (dumping the raw
autocorrelation curve for both real clips' references, lag by lag) shows
no local peak anywhere near the ~280ms figure at all — autocorrelation
just declines monotonically from the smallest tested lag (r≈0.99 at
20ms) down through zero and into negative territory, because a real,
continuously-varying motion curve is *smooth*: neighboring samples
correlate strongly regardless of any periodicity, and that effect
dominates the far weaker periodic signal. The old `dominantPeriodMs`
took a **global max over the whole search range**, which for a smooth,
declining curve is trivially the smallest lag tested — a number that
looks like a period but reflects only smoothness, not periodicity. A
second, independent bug compounded it: `minLag := MinDominantPeriodMs /
stepMs` used integer division, which rounds *down* — so for most
resample steps the search's own floor sat below the documented 150ms
minimum (e.g. 150/40 = 3 → 120ms, not 150ms). Both bugs combined explain
exactly why the reported "period" landed at 100-140ms on both clips: it
was consistently just the search's own (incorrectly low) lower bound,
not any measurement of periodicity.

**Fixed (21 Sep 2026, `funscript/phase.go`):** `dominantPeriodMs` now (a)
rounds the floor up so it never searches below `MinDominantPeriodMs`,
and (b) only accepts a genuine **local** peak reached after the curve's
initial decline — a curve that just declines the whole way through (as
both real clips' references do within a couple hundred ms) correctly
returns 0 ("no confident period"), instead of a spurious floor value.
Locked in by `TestDominantPeriodMsRequiresGenuineLocalPeak`
(`funscript/phase_test.go`): still detects a true synthetic 280ms period
correctly, and no longer returns a period below the floor for a synthetic
signal whose true period *is* below it. `BestLagCorrelation`/
`WindowedBestLagCorrelation`'s reported `LagMs`/`R` are unchanged — this
only affects the `AliasingRisk` diagnostic added in #163.

**Re-measured against the real goldens after the fix:** the flag now
fires on one of the four real pairs — `clip_ausschnitt.funscript`
(`mit_yolo`, whole-clip): `aliasing_risk=true`, `lag_ms=-800`,
`dominant_period_ms=1500`, `alternate_lags_ms=[700, 800]`. The other
three pairs still report `aliasing_risk=false` — their references simply
don't have a confident local autocorrelation peak in [150, 2000]ms, which
is now a considered, honest "no" rather than an artifact of the two bugs
above. **Consequence for option (2):** the board's "wait until the flag
fires on real goldens" condition is now genuinely met on at least one
real pair, with a real (not spurious) period estimate behind it — whoever
picks up tier (2) next has one concrete real-clip case to verify against,
though three of four pairs still show no measured periodicity risk by
this method, so periodicity aliasing is evidently not the sole explanation
for the swinging-lag pattern across all of them.

**What does NOT change**: the practical guidance this finding produced
("don't judge a clip from whole-clip r alone, use windowed measurement")
stays correct regardless of the underlying cause — `docs/
TFTJ_PROFILE_DIRECTION.md`'s citation of this doesn't need reverting.
What changes is that "F-003 (VFR drift)" is no longer the explanation to
reach for when this pattern shows up elsewhere.

## Open inventory

### Already in Go

| Piece | Where |
|---|---|
| CSRT tracker (experimental) | `generator/trackcv` |
| Post-tracking signal path | `generator/posttrack` |
| Opt-in native CSRT generate | `generator/native*.go` |
| Script Doctor + dense doctor | `funscript.EvaluateScriptQuality` / `EvaluateDenseQuality` |
| Device-compat | `funscript.EvaluateDeviceCompat` |
| Phase / FunGen-compare | `BestLagCorrelation`, `WindowedBestLagCorrelation`, CLI `phase`/`compare` (`--window-ms`) |

### Still Python / blocked

| Piece | Notes |
|---|---|
| Non-CSRT backends | `flow` / `grid_lk` / `region_fusion` / `region_fusion_auto` — Python only; tip-matched fusion close to hub on one clip, **no Go port** until `clip_voll` confirms |
| Soft masks, per-scene ROI, AI opinion | Gate native eligibility |
| Auto-ROI / YOLO / AI train | Classical + ONNX helpers |
| Default `generate_funscript.py` fallback | Used when native ineligible / no OpenCV |

**Already Go (do not re-port):** CSRT + two-point/multi-partner (`trackcv`),
`posttrack`, Quality Doctor, phase/windowed FunGen compare, Contact recipe
mapping. Product Generate prefers native Go CSRT when OpenCV-linked.

### Next Go / product slices (quality-first — no loss)

1. **Keep Go CSRT hub default** — best vs FunGen2 on `clip_ausschnitt` (r≈0.590 windowed).
2. **AliasingRisk period floor** — Claude #173 (diagnostic only; does not change lag/r).
3. **Do NOT Go-port** flow / grid_lk / region_fusion until a second golden beats hub.
4. Optional later: Advanced marked `region_fusion` (Python) if `clip_voll` confirms tip-matched ≥ CSRT.
5. Full PTS→device phase chain — media work.
5. Windows OpenCV / accelerator abstraction — later  

### Not now

- 4-zone relative graph as default — measure first  
- Forced 63 BPM / Spatial 3D / Prediction — after measured latency  
- Rust rewrite — profiler gate only  

## Language reminder

Python-vs-Go is not the Tf/Tj correlation problem. Go removes install
burden; perception/timing/ROI semantics still decide quality.
