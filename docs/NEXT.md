# Next steps

## Status at a glance · September 18, 2026 (v0.5.x)

Operational checklist. Measurement history stays below; **what's open now**:

| # | Priority | Status |
|---|---|---|
| 1 | Validate real hardware | **Blocked on you** — Sam Neo 2 + operator |
| 2 | Match FunGen2 references | **Open** — Golden-Clip tool shipped; need real clips |
| 3 | Improve automatic two-ROI suggestions | **Open** — `find_two_rois` not wired (measured insufficient); everyday = auto-tip → CSRT; Zone 2 vibe location — `EVERYDAY_GENERATE.md`. **Fahrplan:** MT-Go→Seed→Speed→ID→Debug in `PRODUCTION_ROADMAP.md` § Multi-track (22 Sep); detail `AGENT_COORD` |
| 4 | Motion-signature/profile GUI | **Done** |
| 5 | Contact-triggered vibration Tf/Tj | **Done** (opt-in); **next:** Normal+Auto default **on** (user can off); partner mark tracked when vib on — `TFTJ_PROFILE_DIRECTION.md` |
| 6–7 | O-markers | **Done** (manual + auto-suggest) |
| 8 | Generator performance vs FunGen2 | **Open-ended** — several closed sub-questions |
| 9 | SAM runtime wiring | **Started** — Densify + RuntimeAdjust + CLI/GUI live scale |

| 10 | Sharper video display | **Closed** (negative) |
| — | Go-native generator | **Automatic** for single-ROI CSRT (CSRT or simpletrack + dense doctor); special cases still Python |
| — | Script Doctor / Phase / Signal≠Fidelity | **Done** (v0.5.0) |
| — | **Stroke preview (extrema + Abtastung)** | **Started** — Stage A library + CLI `stroke-preview` (timing/flags only; not a tracker). Gate: clip_ausschnitt vs hub peaks. See § below. |
| — | **Everyday Generate (FunGen-like)** | **Done (UI)** — auto-find tip → CSRT; Review Improve (trim/fill gaps/audio). `docs/EVERYDAY_GENERATE.md`. Clip gate: hub CSRT windowed r≈0.59. |

Engine direction: [`ENGINE.md`](ENGINE.md). Checklist: [`ROADMAP.md`](ROADMAP.md).

## Verified baseline · September 18, 2026

- Latest published release **[v0.5.4](https://github.com/funfunpayer/SamNPlayer/releases/tag/v0.5.4)**;
  source on this branch targets **0.5.5** (GUI/OFS/OpenCV5/autotune stack).
  See `RELEASE_0_5_5.md`.
- Issue **#95** (generate / OpenCV 5 CSRT) fixed via CSRT→KCF fallback
  (`create_tracker` / package gate); covered by `create_tracker_test.py`.
- CI green on tip stack (#97–#99) before release tag.
- FunGen near-zero-correlation finding (issue #8,
  [`FUNGEN_PARITY_PLAN.md`](FUNGEN_PARITY_PLAN.md)) still describes the
  *general* real-clip gap; priority 2 has found clip-level improvements.

## Verified baseline · September 15, 2026 (historical)

- `main` had merged PRs through #57; source version was advancing toward
  0.3.0 while published tags lagged — superseded by the baseline above.
- Tests (Go race detector, Python, frontend/Playwright) and CI were green
  on `main` as of that merge window.
- The original FunGen near-zero-correlation finding
  ([Issue #8](https://github.com/funfunpayer/SamNPlayer/issues/8),
  [`docs/FUNGEN_PARITY_PLAN.md`](FUNGEN_PARITY_PLAN.md)) still describes
  the *general* real-clip gap; priority 2 below has since found concrete,
  real improvements on individual clips (not yet a closed, general fix).

This is the short-term task list. Architecture and measurements belong in
`HANDOFF.md`, contribution rules in `CONTRIBUTING.md`, and setup instructions
in `WIEDERAUFNAHME.md`. Recheck GitHub status before resuming work.

## Priorities and acceptance criteria

### 1. Validate real hardware · requires a Sam Neo 2 and an operator

Test BLE and Intiface separately: connection, playback, pause/stop,
reconnection, and training. Record raw-value resolution for both channels.
For Tf/Tj, verify that vibration stays off and suction output matches the signal.
Acceptance: record device/adapter details, app version, steps, and observed
results; report deviations as reproducible bugs.

**Research doc added (September 15, 2026):** `docs/SAM_NEO_2_RESEARCH.md` -
the user's own source-tiered research (SVAKOM/FCC/Buttplug/IoST/community,
each tagged by evidence strength) plus a cross-reference note (§9a) that
two of its "not yet known" points - BLE GATT UUIDs and the raw command
bytes - already have PROTOCOL-tier evidence in this repo
(`device/protocol.go`'s `SamNeo2Protocol`, sourced from buttplug-rust's
own maintained code, not guessed). The full measurement plan in that
document (§11/§12: connection timing, per-channel resolution/rise/fall
time, channel interaction, a `SamNeo2DeviceProfile`) is this priority's
concrete acceptance criteria, spelled out in more detail than the summary
above - use it as the checklist once real hardware is available.

### 2. Match user-rated FunGen2 references · active code assignment

Follow [the implementation brief](FUNGEN_PARITY_PLAN.md): establish the
benchmark, isolate tracking errors, improve the shared Tf/Tj path, and retain
both names as compatible aliases for one profile. The user rates FunGen2
closer to the source motion and wants SamNPlayer to approach it. Identical
Tf/Tj output is intentional.

Use reproducible clips with known reference signals alongside representative
real clips. Compare standard and Tf/Tj profiles with documented ROIs and
parameters. Keep raw signals, output, quality reports, and human ratings together.
Acceptance: demonstrate improvements in rhythm, amplitude, and tracking;
report synthetic results separately from real-material ratings.

Per [issue #8](https://github.com/funfunpayer/SamNPlayer/issues/8): prefer
a small, fixed-ROI matrix (2–3 clips with a documented FunGen `.funscript`
reference, ROI1/ROI2 marked once and reused, same parameters, Hub vs Tf vs
Tj) over another large batch — a 22-clip run with Auto-ROI and an invented
ROI2 produced near-zero correlation and did not isolate whether the cause
was ROI or parameters. Compare with best-lag correlation, not same-duration
alone: a correctly-shaped signal shifted in time scores as a total mismatch
otherwise. Only tune `smooth-window`/peaks/`per-scene-roi` after ROI quality
is confirmed. FunGen exports are reference signal only (`.funscript`, not
`.fungen`/`FGPROJ`) — same license boundary as everywhere else in this repo.

**Work Package 1 (benchmark correctness) is implemented:**
`generator/fungen_compare.py` (`python3 fungen_compare.py --dataset DIR
--output report.md`) replaces the local, not-in-repository comparison
script that produced the `compare-fungen-manual.md` baseline. Reviewing
that script's logic (its source was shared in chat, not committed) found
four bugs that independently push correlation toward zero regardless of
tracking quality: it de-duplicated candidate files by path instead of
content (inflating `compare-fungen-manual.md`'s sample count), it aligned
the two files by INDEX after resampling instead of on a shared absolute
timeline or searching for the best lag (so a correctly-shaped but
time-shifted match scored as a mismatch), it returned `0.0` instead of
"undefined" for a constant reference, and it never checked for a
polarity-inverted match (which reads as a strongly negative correlation
and looks like bad tracking rather than a sign-convention mismatch).
`generator/fungen_compare_test.py` has a regression test and gegenprobe
for each, using the synthetic fixtures the brief asked for (known phase,
irregular cycles, pauses, amplitude changes, offset timestamps, constant
signals).

**Run against real clips, shared in chat rather than on disk here (two of
the three valid moving references from `compare-fungen-manual.md`, plus
their SamNPlayer outputs):** correlation stayed weak (r≈0.05–0.13) even
after lag and orientation search, including on a single continuous
chapter with no timing gaps. The four benchmark bugs were real but do not
explain the whole gap. The persisted manifest (hashes, generator
versions, ROIs) from the brief's "Reproduce before changing the
algorithm" section is **implemented** as the Golden-Clip Benchmark
(`generator/golden_clip_benchmark.py` + GUI "Bench" tab, PR #70);
a proper scan against the user's full `funscript-tests` folder still
needs the operator to point the manifest at real clips (local to their
machine).

**Work Package 2 (isolate the mismatch) found a real, fixed bug:**
`track_two_points()` measured the two-region distance using only the Y
coordinate of each box's center, not a full 2D distance (the brief's
zoom-invariance concern, section "Investigate ... test that claim and
correct it", also flagged the underlying formula). This is harmless when
ROI1/ROI2 sit at different heights, but produces a near-zero, tracking
quality-independent signal whenever they sit at the same height — exactly
what issue #8 describes the batch's ROI2 heuristic doing ("shift ROI1 by
~1.2× width", i.e. no height change). Confirmed on the real BBW/Entladen
`tj` outputs shared in chat: both sit flat at one extreme for most of
their length, then move briefly - the signature of a distance metric
seeing no variation until tracker drift accidentally introduces some.
Fixed: `two_point_distance()` now computes the full 2D center-to-center
distance (`np.hypot`), a strict generalization that keeps the existing
vertical-signal test (`two_point_test.py`) passing and adds
`two_point_axis_test.py` for the horizontal case, with a gegenprobe
(reverting to the Y-only formula reproduces the near-dead signal:
amplitude 1.0px instead of the true 80px).

**Run against a large synthetic dataset (48 clips, both a FunGen 2 export
and a SamNPlayer hub+tf run per clip, shared in chat as `fungen2.7z` /
`samnplayer_test_funscripts.7z`, generated the same day with the 2D-distance
fix and `auto_roi.find_two_rois` for ROI2, not the invented-shift
heuristic):** far stronger than the real-clip result — mean r ≈ 0.52 (hub)
/ 0.58 (tf) across ~30 valid clips (walk cycles, sine/triangle/square
waves, camera pans/zooms, occlusion, irregular events; 28 of 60 rows were
excluded as undefined, mostly constant FunGen references for clips FunGen
itself found nothing scriptable in). This is a large jump from the
real-clip r≈0.05–0.13 - clean, single-subject synthetic motion is
evidently far easier for both tools than real material with occlusion,
non-rigid deformation and multiple bodies. Confirms the earlier read: the
comparison-methodology bugs and the two_point axis bug were real and
worth fixing, but real-material tracking quality is its own, still largely
open problem - synthetic-clip success does not by itself predict
real-clip success.

**A methodology risk found while checking this:** the first pass used the
tool's old 3000ms default lag search window, which pushed several
short-clip correlations toward the search boundary (lag=±2800-3000ms) -
the sign of a wide search "finding" a coincidental match on a small
overlap window rather than a real delay. Narrowing to 500ms dropped the
mean from 0.696 to 0.486. Fixed in `fungen_compare.py`: the default
`--max-lag-ms` is now 1000 (was 3000), every result carries a
`low_confidence` flag when fewer than `LOW_CONFIDENCE_SAMPLES` (30, i.e.
under 3s of overlap at the 100ms resample step) samples were used, and
`format_report()` prints a confidence-excluded mean alongside the plain
one so a few short, lucky clips cannot quietly carry the average. On this
dataset the two means come out close (hub 0.517 vs 0.518, tf 0.581 vs
0.564), which is itself a good sign - the result isn't an artifact of a
handful of short overlaps.

Also added: `fungen_compare.py --dataset` now scans subfolders
(`rglob`, not `glob`) - the real dataset's references and batch output
each came in their own subfolder tree, which the original top-level-only
scan would have silently found nothing in.

**Done for one real clip (September 14, 2026):** a real 42s titjob clip
was shared with its source video, its FunGen 2.6.3 reference, and an
already-existing SamNPlayer `tj` output. The video is AV1 in an unusually
low 256x144 resolution (transcoded to H.264 with `ffmpeg`/`libdav1d`
first - the OpenCV build here has no working AV1 decode path); this alone
is a plausible confound, since CSRT tracks small, blurry frames worse
than a normal-resolution source would. ROI1/ROI2 were placed by hand on
the first frame (tip vs. a lower, mostly-static anchor point) and run
through the current (post-fix) `generate_funscript.py --profile tj`.

Both the pre-existing output and the fresh run land far above the
r=0.05-0.13 range from the earlier real clips: r=0.336 (existing output)
and r=0.356 (fresh run), both against an inverted polarity match (FunGen
and SamNPlayer disagree on which direction is "up" for this clip - a
sign-convention question, not a tracking-quality one; `fungen_compare.py`
already detects and reports this rather than scoring it as a mismatch).
That is a real improvement over the earlier real-clip numbers, but this
one data point does not by itself prove the fix generalizes: the fresh
run's own Quality Doctor score was 0.60 ("PRÜFEN") because the CSRT
tracker lost at least one of the two regions in 75% of frames (holding
the last known position while lost, per `track_two_points`'s documented
behavior), and shape-normalized error stayed high (~0.93) despite the
respectable correlation - a sign that phase/direction line up passably
but the matched amplitude does not. Likely explanation: the very low
source resolution plus heavy motion blur, not the two-point distance
formula itself (which is what the earlier fix targeted). A higher-
resolution source video would be needed to tell tracking-quality
limitations apart from resolution limitations on this specific clip.

**Still not done:** running this same check across more than one real
clip - one data point does not establish a trend, and every clip shared
so far except this one still lacks its source video.

**Follow-up on this same clip (September 14, 2026):** the 75%-lost-frame
result was suspected to be a resolution/motion-blur limitation. Tested
that directly with two changes, both measured against the same 256x144
clip using `track_two_points` and `fungen_compare.best_lag_correlation`:

- **Upscaling frames before tracking, to test the "CSRT needs more
  pixels" hypothesis: measured WRONG.** 2x/3x upscale (`INTER_CUBIC`)
  made lost-frame rate markedly *worse* (2% baseline -> 6% at 2x -> 34%
  at 3x), not better - CSRT's correlation filter apparently loses lock
  more often as its search window grows on a blurry source, the opposite
  of the hypothesis. Documented as a negative result in `create_tracker`'s
  own docstring so nobody re-tries it without new evidence. Not shipped
  anywhere (this was a standalone measurement, not a pipeline change).
- **Tighter, more deliberately placed ROI1/ROI2 (same resolution, same
  CSRT): lost-frame rate dropped from 75% to 2%, Quality Doctor score
  from 0.60 to 0.95.** This alone is a strong result - it means the
  earlier 75% figure was much more about *how precisely the two regions
  were marked* than about the clip's resolution or blur, which is the
  more actionable/generalizable lesson of the two.

**Follow-up (September 14, 2026), testing the user's own idea of
improving a poor-quality source video before tracking rather than
after:** unsharp masking (`cv2.GaussianBlur` + `addWeighted`, directly
targeting the clip's documented motion blur, unlike upscaling which
targeted resolution) applied to every frame before CSRT tracking, same
tip/lower-cleavage ROI pair as the 2%-baseline above. Small, one-
directional improvement, not the regression upscaling caused: lost
frames dropped from 51/2527 (2.0%) at amount=0 to 48/2527 (1.9%) at
amount=0.5 to 45/2527 (1.8%) at amount=1.0 - real but modest, since this
ROI pair was already performing well and had little room to improve.
**Incomplete:** the amount=2.0 run and a FunGen-correlation check (the
metric that actually mattered in the ROI2 ablation above - lower
lost-frame count did NOT predict better correlation there) were not
finished before this investigation was cut short for time. Before
shipping sharpening as a preprocessing step: confirm the trend holds on
a clip that's actually struggling (this one wasn't, so there's limited
signal here), and check FunGen correlation, not just lost-frame count,
given what the ROI2 ablation already taught about that gap. Not
implemented in the pipeline - measurement only, script not kept.

**Revisited the same day, user asked again about gentle upscaling
specifically** (a smaller factor than the already-rejected 2x/3x,
possibly combined with the sharpening above) - measured via a
background agent on a small, deliberately hard 18x16px tip ROI (the
larger ROIs used elsewhere floor at 0% lost frames, leaving no room to
see any effect): **no clean, trustworthy signal, and the completed
amount=2.0 sharpening run from above.**

| condition | lost-frame rate |
|---|---|
| baseline (no preprocessing) | 16.90% (427/2526) |
| upscale 1.15x only | 52.30% (1321/2526) |
| upscale 1.3x only | 1.58% (40/2526) |
| unsharp amount=2.0 only | 18.05% (456/2526) |
| upscale 1.3x + unsharp amount=1.0 | 1.23% (31/2526) |

The headline is the *instability*, not a direction: 1.15x more than
tripled the loss rate while 1.3x cut it to a tenth - the opposite of a
smooth dose-response, and inconsistent with the earlier 2x/3x result's
clean monotonic trend. Most likely explanation: a tiny 18x16 ROI is
very sensitive to sub-pixel resampling artifacts specific to each exact
scale factor, not a real property of "upscaling" as a technique. Single
run per condition, single ROI, no repeats - this does not rule out a
real effect at some untested factor, it rules out treating "gentle
upscaling helps" as established. The amount=2.0 sharpening result
completes the interrupted sweep from above and is a real, if small,
regression rather than a continuation of the 0/0.5/1.0 improving trend
(18.05% vs. the 1.8% at amount=1.0) - suggesting the earlier trend
overshoots somewhere before 2.0, another reason not to pick a sharpening
amount from three points without checking the curve bends back. The
upscale+sharpen combo numerically beat both single-technique runs, but
given how erratic the upscale-alone result was, that reads as riding
the same 1.3x instability rather than a proven synergy - **not enough
to ship**, same bar as the rest of this section. Verdict for the user's
question: gentle upscaling is not shown to help, and given how the
result flipped between two nearby factors on this one ROI, it would
need a proper multi-ROI, multi-clip sweep (matching this document's own
`Golden Clip test suite` idea, see `docs/SAM_ARCHITECTURE.md`) before
trusting any specific factor, not a quick re-test.

**But this did NOT translate into a better FunGen match - if anything
the opposite, and this is the more important finding to carry forward.**
Three attempts on the identical clip, ranked by own tracking quality
(Quality Doctor / lost-frame rate) next to their correlation against the
FunGen `.funscript` reference:

| run | own quality | lost frames | r vs. FunGen | lag |
|---|---|---|---|---|
| pre-existing output | 1.0 | (not re-measured) | 0.336 | -600ms |
| earlier session's "fresh run" | 0.60 | 75% | 0.356 | (not recorded) |
| this session's precise-ROI run | 0.95 | 2% | **0.070** | -1000ms (search boundary) |

The run with by far the *best* own tracking quality has by far the
*worst* correlation to FunGen's reference - and its best-lag search
pinned at the -1000ms boundary, the same "coincidental match on a short
overlap" warning sign the lag-search-window methodology fix (earlier in
this section) was written to catch. Own tracking robustness and match-
to-FunGen-reference look like two largely independent axes on this
clip, not two views of the same underlying quality.

**Follow-up ablation, same session:** tested whether the choice of ROI2
anchor - not tracking quality - is the actual lever, by holding ROI1
(the tip, `115,42,22,28` in this clip's 256x144 frame) fixed and varying
only ROI2, plus a single-point `standard` (hub) run with no ROI2 at all
as a baseline for "is two-point distance even the right framing":

| run | ROI2 | own quality | r vs. FunGen | lag |
|---|---|---|---|---|
| precise-ROI tj (above) | lower cleavage, `110,92,35,25` | 0.95 | 0.070 | -1000ms (boundary) |
| single-point hub, tip only | none | 1.00 | 0.129 | **0ms** (confident) |
| tj, neck/collarbone anchor | `110,5,35,22` | 0.85 | 0.123 | +600ms |

Switching only the ROI2 anchor point (lower cleavage -> neck) nearly
doubled the correlation (0.070 -> 0.123) with everything else held
fixed - direct evidence that ROI2 choice, not tracking robustness, is
the dominant lever for this profile on this clip. The single-point hub
signal (no second region at all) did comparably well to the *better* of
the two two-point attempts, with the most trustworthy lag of the three
(0ms, not pinned to the search boundary) - some real signal is being
captured by simple tip motion alone, without needing a distance at all.
None of these three, however, beat the two older/messier runs from the
row above (r=0.336, r=0.356) - so this still doesn't identify a
reliably better ROI2 heuristic, only that ROI2 choice matters a lot and
"lower, mostly-static point near the target" is not obviously the right
default intuition (neck clearly beat lower-cleavage here).

**Net conclusion, still one clip:** chasing tighter CSRT tracking
further is not obviously the right lever for closing the FunGen gap;
which ROI2 anchor point (or whether the two-point-distance framing is
right at all for this profile) is the more promising open question, but
needs more real clips - and ideally more than 2-3 candidate anchors per
clip - before drawing a firm conclusion. The ablation method itself
(hold ROI1 fixed, vary ROI2, compare via `fungen_compare.
best_lag_correlation`) is cheap and reusable for whoever picks this up
next with more clips - no new tooling needed, `generate_funscript.py`
and `fungen_compare.py` already cover it end to end.

**Same clip, September 15, 2026 - the auto-detected "hub" region beats
the manually-placed tip-only point.** Asked directly to test this clip
again and look for FunGen parity, so re-ran the single-ROI (`standard`)
case with the region `auto_roi.find_roi()` actually proposes on this
clip's first frame (`61,63,154,72`, a much larger 154x72px area than the
hand-placed tip-only `115,42,22,28` from the ablation above) through
`generate_funscript.py` at the GUI's own defaults (`--adaptive-keyframes
6`), then compared with `fungen_compare.py --max-lag-ms 300` - narrow on
purpose, per this section's own "a wide search finds a coincidental
match" risk, against the same FunGen 2.6.3 reference used throughout this
clip's prior measurements.

Result: **r=0.310 at lag=0ms**, `n=150` samples, not low-confidence,
orientation normal (not inverted, unlike the `tj` result above), own
Quality Doctor score 0.55. That beats the tip-only single-point hub
result from the ROI2 ablation above (r=0.129, same clip, same 0ms
lag-confidence) by a wide margin, and sits well above the general
real-clip range (r=0.05-0.13) from the top of this section - though
still below the two messier/hand-tuned two-point runs (r=0.336/0.356),
and shape error stayed high (0.933): phase/direction line up
respectably, matched amplitude still does not, the same gap noted
throughout this clip's measurements.

Also re-ran `tj` on the same clip with a deliberately careless,
unplaced ROI2 (`20,20,40,30`, picked without looking at the frame) as an
honest negative control, not a real attempt: r=0.167 - clearly worse
than the careful hand-placed ROI2 sweep above, consistent with (not a
new data point beyond) this section's already-established "ROI2 choice
is the dominant lever" finding.

**Reading, still one clip:** the current auto-`find_roi()` region does
noticeably better than a hand-picked tip-only point for the single-ROI
case here - real, if single-clip, evidence in favor of priority 3/4's
direction (auto-detection as the GUI default for ROI1), on top of the
acceptance-criteria discussion already there. Not "ship it as default"
by itself (one clip, and priority 3's bar is broader than one number),
but a genuinely encouraging result rather than a null one - worth
re-running this same `find_roi()`-vs-manual comparison on the other real
clips already shared in chat this session (the BBW and Entladen clips,
both with a FunGen reference already available) before drawing a firmer
conclusion.

**Correction, found the same day while investigating `grid_lk`'s own
FunGen correlation:** the r=0.310 hub result just above is wrong - not a
methodology flaw in `fungen_compare.py` this time, but a stale CLI flag.
The generation run behind it still carried `--max-frames 900` left over
from an unrelated earlier task in the same session (checking curve
smoothness on a quick sample) - so it only covered the FIRST 15 OF THE
CLIP'S 42 SECONDS, not the whole thing (`n=150` samples above is the
tell: 150 * 100ms ≈ 15s, not the ~421 a full-clip comparison at this
resampling step actually produces). Caught while generating a matching
`grid_lk` run on the SAME roi for comparison and noticing its `n=421`
didn't match the hub row's `n=150` from the same clip.

**Re-measured on the actual full clip** (same ROI `61,63,154,72`, same
`--adaptive-keyframes 6`, same `fungen_compare.py --max-lag-ms 300`):
**r=0.179 at lag=0ms**, `n=421`, shape_err=1.063 - real and still above
the tip-only single-point result (r=0.129) and the general real-clip
floor (r=0.05-0.13), but by a much smaller margin than the withdrawn
0.310 figure suggested. The qualitative reading from above (auto-detected
hub region beats hand-picked tip point, still one clip, not enough to
flip priority 3/4's default) still holds - the number behind it was just
wrong. The `tj` row above (r=0.167, careless ROI2 negative control) has
the same `--max-frames 900` flaw and is likewise short-window, but since
that row was already flagged as "not a real attempt, not written up as a
finding," it wasn't re-run - nothing rests on that number.

**`grid_lk`'s own FunGen correlation, same full clip, same ROI, same
`--adaptive-keyframes 6` (the actual reason this correction was found):
r=0.125 at lag=0ms**, `n=421`, shape_err=1.089 - noticeably *worse* than
CSRT's corrected 0.179, despite `grid_lk`'s own Quality Doctor score
being *higher* on this run (1.00 vs CSRT's 0.55). Same pattern already on
record from the ROI2 ablation earlier in this section ("lower lost-frame
count did NOT predict better correlation") - this project's own internal
quality signals (smoothness, lost-frame rate) and actual agreement with a
human-made FunGen reference are evidently two different things, not
proxies for each other. Not a reason to reconsider shipping `grid_lk`
(that decision rested on speed and the KCF/MOSSE-style collapse risk, not
on FunGen correlation, and CSRT itself only reaches 0.179 here - neither
backend is close to matching FunGen on this clip), but a real data point
for whoever next asks "does X track more like FunGen": own-quality
metrics are not a substitute for measuring that directly.

### 3. Improve automatic two-ROI suggestions

Inspect `find_two_rois` and existing tests before making changes. Compare
automatic suggestions against manually selected regions and known ground truth.
Acceptance: measurable benefit, visible uncertainty, and manual correction.
Do not switch profiles automatically without validation.

Per issue #8: **never** batch a Tf/Tj (distance-profile) run with an
invented/heuristic ROI2 (e.g. "shift ROI1 by ~1.2×width") — that alone can
explain a weak correlation before any parameter is even considered. Until
`find_two_rois` is measurably good enough, Tf/Tj needs a real, manually
placed second mark, or the AI ROI adapter's suggestion (`--roi-finder ai`,
`docs/AI_ADAPTER.md`) with a human confirming or correcting it — never a
silently auto-committed guess for either ROI.

The user's direction (September 14, 2026): automatic detection for both
ROI1 and ROI2 should become the *default* path in the GUI once it is
measurably good enough (per the acceptance criteria above), manual dragging
becoming the fallback/correction instead of the everyday first step. This
does not mean building object detection from scratch for ROI1 —
`generator/ai_roi.py` (local ONNX, PR #6) already implements exactly the
"detection proposes a region" idea, and just needs GUI wiring (priority 4).

**Correction, found while starting that wiring:** `find_two_rois()` in
`auto_roi.py` already carries its own measured verdict, in the docstring -
"GEMESSEN UNZUREICHEND, NICHT IM ERZEUGUNGSPFAD VERDRAHTET" ("measured
insufficient, not wired into the generation path"). On a real two-object
test clip: manually chosen regions correlate at +0.62 with the known
distance signal; the automatic grid-based pairing manages only +0.17 to
+0.28 across three grid resolutions, because the two points sit close
enough (~25px apart in that test) to fall into the same or adjacent grid
cells, and a finer grid barely helps. So ROI2 (the ROI1↔ROI2 distance
pair Tf/Tj needs) is **not** a "wire it and validate" situation like
ROI1 - it is already validated and found not good enough. Making it the
GUI default now would be a regression, not an improvement, and would
violate issue #8's already-learned lesson (never auto-commit an
unvalidated guess for either ROI). Two directions worth trying before
revisiting "default on": (a) the AI detector proposing ROI2 too, once it
can find distinct objects instead of a rhythm-scored grid cell - untried,
plausible since it doesn't share the grid-resolution problem; (b) improving
`find_two_rois`'s candidate search past a fixed grid. Until either is
itself measured against manually chosen regions, ROI2 stays manual-first
with AI/heuristic suggestions as an opt-in aid, not swapped roles.

**Direction (b) tried (September 15, 2026):** replaced the grid-cell-pair
candidate search with `_peak_regions()` - grows a connected region around
each strongest still-unused cell in turn (removing it before searching for
the next), instead of treating single grid cells as candidates and
rejecting adjacent-cell pairs. That rejection rule was the actual cause of
the old +0.17-0.28 result: two close objects mostly fell into the same or
neighboring cells, and "spatially separated" threw out exactly the pairs
that best represented each object.

Measured on two synthetic scenes (`generator/auto_roi_two_point_test.py`,
both with camera pan + shared vertical motion + a swinging distance as the
real signal, matching this project's established two-point test
construction): the original ~25px-apart case from the docstring above
(worst case for the old method) and the wider-apart case
`two_point_test.py` already uses for `track_two_points` itself.

| scene | old (grid-cell pairs) | new (`_peak_regions`) |
|---|---:|---:|
| close (~25px apart) | +0.01 | **+0.63** |
| further apart | +0.17 | **+0.21** |

Both improved, the close case by far the most - the case the old method
handled worst. The close case even edges past the docstring's own
hand-picked-region baseline (+0.62) on this synthetic scene, though that
is not a claim the automatic method now beats manual placement in
general - one synthetic scene, not real material.

**Caveat found while tuning:** the growth parameter (`decay`, how far a
region extends from its peak cell) is NOT a smooth dial - 0.4 and 0.6
both scored well, but 0.5 (between them) collapsed the wider-apart case to
-0.03 and 0.7 collapsed the close case to -0.80. Shipped 0.4 as the
center of the wider stable range (0.3-0.4 both stayed clearly positive on
both scenes) rather than 0.6's marginally higher single-run number,
following this document's own "gentle upscaling" lesson (priority 8) about
not trusting a narrow parameter sweep's best point over its shape.

This alone was real, measured improvement on synthetic material only - not
yet the "validated against manually chosen regions on real clips" bar
priority 3's acceptance criteria and the user's own direction above
require before ROI2 auto-detection can become the default.

**Run against the real clip (September 15, 2026), the very next step this
section itself called for:** `find_two_rois()` on the actual `clip_h264.mp4`
(the one real clip with its own source video in this session's dataset)
first proposed `ROI1=(0,0,256,144)` - the **entire frame** - and
`ROI2=(149,0,106,72)`. That is a real limitation found on real material,
not a synthetic-scene artifact: real footage has widespread camera-motion-
compensated residual movement across many cells, and `_peak_regions()`'s
best-first growth had no cap and kept absorbing weakly-elevated neighbors
across nearly the whole grid - the failure mode the two clean synthetic
scenes above didn't exercise at all, because outside their two objects
almost nothing sat above the threshold.

**Fixed the same day**, not left as a known issue: `_peak_regions()` now
caps both cell count (`max_cells`, default 1/8 of the grid) and bounding-
box span (`max_row_span`/`max_col_span`, default 1/3 of each grid
dimension) - a candidate cell that would blow either budget is skipped
(left available for a later region) rather than absorbed. New
`generator/auto_roi_region_bounds_test.py` reproduces the real clip's
"diffuse near-threshold background + two clear peaks" pattern directly on
a synthetic score grid (no video needed) and asserts both regions stay
compact. Re-ran on the real clip after the fix: `ROI1=(213,108,42,36)`
(4% of frame area), `ROI2=(106,36,85,36)` (8%) - both now plausible,
object-sized regions, matching the two clean synthetic scenes' behavior.

Compared both the buggy (whole-frame) and fixed (compact) proposals
through `generate_funscript.py --profile tj` (both backends) against the
same FunGen 2.6.3 reference this clip's other measurements use
(`fungen_compare.py --max-lag-ms 300`):

| run | r vs. FunGen | lag |
|---|---:|---:|
| whole-frame ROI1 (pre-fix), CSRT | +0.246 | 0ms (confident) |
| whole-frame ROI1 (pre-fix), grid_lk | +0.075 | -200ms |
| compact ROI1/ROI2 (post-fix), CSRT | **+0.038**, inverted match | -100ms |
| compact ROI1/ROI2 (post-fix), grid_lk | **+0.044**, inverted match | -200ms |

**Not the outcome that would make a clean story, and reported as such:**
the buggy whole-frame version scored *higher* than the fixed, properly-
bounded one. This is not a reason to keep the bug - an unboundedly
growing region is wrong regardless of outcome, and the whole-frame
result's +0.246 rode on `ROI1` being too large to move at all (so the
"distance" degenerated toward `ROI2`'s own motion against a fixed point),
not on the method correctly separating two objects. But it does mean the
fix did not reveal a good automatic ROI2 result on this clip - both
backends land near zero (practically no signal, and an inverted-polarity
best match) with the now-compact, sane-looking proposals. This lines up
with, rather than overturns, `find_two_rois`'s existing "GEMESSEN
UNZUREICHEND" verdict: one more real clip confirming automatic ROI2
placement isn't there yet, not a real clip that flips the picture.

**Still not enough to change the GUI-default question either way** - one
real clip, the same "needs more real clips" ceiling every measurement in
this section runs into. `find_two_rois` remains unwired from the
generation path.

### 4. Complete motion-signature and profile integration in the GUI

The naming and persistence paths now exist at the CLI level
(`--label-scene`, `--suggest-profile` in `generate_funscript.py`, PR #7) —
this item is now specifically the GUI wiring: a "name this scene" control
and a suggestion display, not building the underlying mechanism from
scratch. Acceptance: names and parameters survive restarts; reuse for
similar scenes is offered with an explanation and can be declined.

The user's direction (September 14, 2026), same priority as above: wire
`--roi-finder ai` / `AutoDetectROI` (already implemented, see #6/#7 and
`docs/AI_ADAPTER.md`) into the generator tab so AI-proposed regions,
profile, and quality opinion appear automatically as soon as a video is
loaded, with one-click accept and the existing manual drag as correction —
not a separate step the user has to remember to trigger. General direction
for the player/generator GUI: reduce how much the user has to configure by
hand for the common case (progressive disclosure - move settings that are
rarely changed, e.g. tick rate, smoothing, RDP tolerance, behind an
"Advanced" section instead of a flat list), while keeping all existing
settings available, not removing configurability. More options are good;
what needs to improve is which ones are visible by default.

**First slice done:** `generator/ai_roi.py` gained `--check` (reports
availability without opening a video, for the GUI to enable/disable the
option). `generator.go` gained `FindROIAIWithProgress`/`AIRoiAvailable`,
sharing the existing stdout/stderr protocol with the classical path via a
new `findROIViaScript` helper (`AutoDetectROI` and `FindROIWithProgress`
are behavior-preserving refactors, not rewrites). The generator tab shows
a "KI-Erkennung (ONNX)" checkbox next to "Region automatisch finden",
enabled only when `CheckAIRoiAvailable()` says so; Settings gained an
optional model-path field. Region proposal only (not profile/quality yet,
and not the two-region case - see the ROI2 correction above). Not
end-to-end tested with a real model (none available to test with); the
`--check`/unavailable path and the classical path are what's verified.

**Also fixed while wiring this (found by the user, September 14, 2026):**
every one of the ~11 Python subprocess calls in `generator.go` (dependency
check, first-frame preview, ROI search, generation itself, ...) flashed a
console window on Windows - `exec.Command` never set `HideWindow`. Fixed
with a `command()` wrapper (`generator/exec_windows.go`/`exec_unix.go`,
same pattern as `update/update_windows.go`'s existing `detachedSysProcAttr`)
used everywhere `exec.Command` was called directly in that file.

### 5. Contact-triggered vibration for Tf/Tj — implemented, opt-in

The user's direction (September 14, 2026, refined twice the same day):
when the tracked tip (ROI1, e.g. the glans) touches or grazes the
reference point (ROI2 - nipple **or** tongue, either works, same
mechanic) - i.e. the ROI1↔ROI2 distance drops near its minimum -
vibration should pulse proportionally to how close/strong the contact
is, "not just brief, it should fit the material" (duration/strength
should track the actual clip, not a fixed blip); suction keeps its
existing mapping unchanged. Wanted "whether with AI or without" -
independent of which engine found the regions, this is a mapping/recipe
change, not a detection change. Explicitly asked to research the
approach before building it ("am besten recherchieren dazu").

**Research finding that shaped the design:** the "material fit"
requirement turned out to already be answerable from data the pipeline
already has. For tf/tj, `pos` (0-100, clamped 20-90 by
`clamp_actions_pos`) **is** the inverted, per-video-normalized ROI1↔ROI2
distance ("Enge Distanz = hohe pos", `tf_tj_meta.py`) - `positions_to_
funscript`'s percentile normalization already scales it to that specific
clip's own observed range. So "contact" doesn't need a new signal or a
fixed pixel/percent threshold: it's the top slice of that script's own
`pos` range, and deriving the vibration envelope directly from `pos`
itself during that slice - instead of a fixed-length pulse - makes
duration and intensity inherit exactly how long/deep contact actually
lasts in that clip. A short graze produces a short blip; a lingering
deep stroke produces a longer, stronger one.

**Implemented as an opt-in "Kontakt-Vibration" checkbox** (generator
tab, shown only for tf/tj, next to the existing Tf/Tj hint - default
off, so the existing no-vibration behavior is unchanged unless
explicitly requested):
- `generator/tf_tj_meta.py`: `device_recipe_for`/`apply_profile_metadata`
  take `contact_vibration=False`; when true, `device_recipe.contact_
  vibration = True` is written into the funscript's metadata (field
  omitted entirely when false, so older players that don't know the
  field see the same file they always would).
- `generate_funscript.py --contact-vibration` (CLI flag) → `generator.
  Options.ContactVibration` (Go) → `GenerateOptions.contactVibration`
  (Wails binding) → the checkbox in `generator.js`.
- `funscript.MapOptions.ContactVibration` (new field, `mapper.go`):
  when set and `Sync == SyncSuctionPosition`, `ToIntensityCurve` first
  scans the whole script's own `pos` min/max once, then - only above the
  top quarter of that per-script range (`contactVibrationSpan = 0.75`) -
  ramps vibration from 0 to 1 as `pos` approaches its peak, and back to
  0 as it recedes. A script with too little `pos` variation overall
  (`contactVibrationMinSpan`, <5 points) disables the effect entirely
  rather than buzzing on ordinary movement. Below the threshold,
  vibration stays exactly 0, same as before - the change is additive,
  gated, and doesn't touch suction's mapping.
- `app_playback.go`: reads `Metadata.DeviceRecipe.ContactVibration` from
  the loaded script (not a playback-time toggle - the choice is baked in
  at generation time, matching how `profile` itself already flows) and
  sets it on `mapOpts` before building the intensity curve.
- The pre-existing invariant test (`TestRecipeTJSuctionOnlyNoVibration`,
  `funscript/recipe_test.go`) is untouched and still passes - it exercises
  the default (flag off) path. New tests cover: off-by-default behavior
  unchanged even with a peak that would otherwise register as contact,
  vibration tracking a simulated contact window and returning to 0
  outside it, and the low-span guard. Mirrored on the Python side
  (`tf_tj_meta_test.py`).

**Real-clip visual validation done (September 14, 2026), same session as
the ROI2 ablation above:** ran `--contact-vibration` on the same real
clip (tip + lower-cleavage ROI2, the pair from the ablation table) and
cross-checked the resulting vibration curve against the actual video
frames. Strong match: at a low-vibration timestamp (vib=0.22 @ 950ms)
the tip sits pulled back, visible above the breasts; at a
vibration=1.00 timestamp (2700ms) the tip has fully disappeared into
the cleavage, right at the ROI2 anchor - exactly the "deep contact"
state the feature is meant to detect. The design (envelope derived
directly from the distance signal, not a fixed pulse) also showed up
as intended in the data: this clip's rhythm keeps the tip near the
cleavage for a large share of its length (not a brief instant), and
the vibration curve tracks that faithfully rather than firing a short
blip - confirming "fits the material" rather than assuming contact is
always brief.

Not yet done: ~~the pulse *shape* was deliberately left open by the user
("their call") - the current envelope is a straight linear ramp against
distance-to-peak, the simplest option consistent with "fits the
material," and is now visually validated as tracking genuine contact
rather than firing on tracker noise.~~ **Done as product controls
(September 17, 2026):** generator tab now exposes Empfindlichkeit
(ContactVibrationSpan, default 0.75) and Kurve (linear / soft=t² /
peak=√t); values land in `device_recipe` and drive `ToIntensityCurve`.
Playback can disable contact vibration for one session without
regenerating; the curve canvas draws vibration as a second (orange)
track when the recipe has it; log emits „Kontakt-Vibration aktiv“.
Still open: no listening/feel test on real hardware yet (needs
priority 1).

**Signal quality for contact vibration (September 17, 2026):** when either
Tf/Tj tracker loses its target, the held last-known distance would keep
buzzing — generation now writes `metadata.tracking_gaps` and the mapper
forces vibration to 0 inside those windows (suction unchanged). A short
dedicated contact-envelope smooth (default 0.45) damps tracker jitter on
the vib channel only. Tf/Tj „Region automatisch finden“ now calls
`find_two_rois` / `ai_roi --two` as an opt-in **suggestion** (fills ROI1+ROI2
for the user to correct; never silently committed — still not the default
path for batch/auto without confirmation).

**Brief/grazing contact, checked (September 15, 2026):** the other open
question - whether the envelope also reads as natural on a quick
touch-and-release rather than the one real clip's sustained contact -
didn't need new video material to answer, since the envelope is derived
purely from the script's own `pos` values, not from wall-clock duration.
Added `TestRecipeTJContactVibrationBriefGraze` (`funscript/recipe_test.go`):
a synthetic script with a 50ms touch-and-release next to one with the
existing 300ms sustained contact, both reaching the same observed peak
position. Confirmed rather than assumed: both reach the same peak
vibration (≥0.9, as designed - strength comes from *how close* to the
peak, not from *how long*), but the brief touch stays above 0.5 for far
fewer frames than the sustained one, and produces a real, non-zero pulse
rather than being missed entirely by the threshold. Closes this open
question; only the real-hardware feel test above remains.

### 6. Climax ("cum") detection — new AI feature, user marked urgent

The user's direction (September 14, 2026, called it "dringend" - urgent):
detect the climax/ejaculation moment in the source video and use it to
drive playback (presumably feeding priority 5's contact-vibration idea
and/or the "O-function" in priority 7 below - the user did not fully spec
how the three connect, only that all three are wanted).

Not designed and not started. This is a content-classification problem,
materially different from the existing region/profile/quality AI slices
(`docs/AI_ADAPTER.md`) which measure motion, not scene content - it would
need its own model (or a Colibri vision-judgment call, same pattern as
`ai_quality.py`'s second opinion) and, like priority 5, a deliberate
design pass before implementation: what signal it outputs (a single
timestamp? a confidence curve? an event marker written into the
funscript, tying into priority 7?), how false positives/negatives are
handled (a wrong or missed detection changes device output, not just a
displayed label), and whether it's AI-only or has a classical fallback
signal too (consistent with `docs/AI_ADAPTER.md`'s "AI proposes, classical
system always measures/decides" rule - needs an answer for what the
classical measurement would even be here). Needs a design decision with
the user before implementation.

### 7. Author "O-function" event markers into the funscript itself

The user's direction (September 14, 2026): today's Extended-O behavior
(`prefPlaybackEOEnabled` and related settings in `app_settings.go`/
`settings.js`) is detected live by the *player* from the played signal.
The user wants an additional option: mark/select an "O-function" region
directly in the funscript at generation/edit time - so it's authored data
carried in the script, selectable by the user, instead of only inferred
at playback. Explicit: the existing player-side detection should stay
("da soll es drin bleiben, aber erkannt werden") - this is additive, not
a replacement.

Raises a real format question, also not decided: the standard
`.funscript` JSON (`actions: [{at, pos}]` + a free-form `metadata` object,
see `funscript/funscript.go`) has no standard field for named
event/chapter markers. The user pointed out FunGen also carries its own
additional, non-standard data alongside the standard fields - i.e.
extending `metadata` with a SamNPlayer-specific optional field (e.g. a
list of `{startMs, endMs, kind}` markers) would be consistent with how
other tools already do this, and wouldn't break compatibility with
plain `.funscript` consumers that only read `actions`.

**Refined the same day:** the marker pattern itself is now specified -
one primary marker shortly before the climax point, plus optionally one
or two secondary markers earlier in the scene at lower intensity ("nicht
so doll" - not as strong). Generation-time opt-in, same UI pattern as
priority 5's checkbox and the existing AI-quality-opinion checkbox: a
toggle to include or omit O-markers in the script, decided when the
script is created, off by default.

This placement logic is well-specified and simple to implement *once a
climax timestamp exists to place it relative to* - but that timestamp is
exactly what priority 6 (cum detection) would have to supply, and that
detector doesn't exist yet. So priority 7's own implementation is
blocked on priority 6's detection-approach decision, not on anything
about the marker format or UI, both of which are now clear enough to
build as soon as an input timestamp is available (even a manually placed
one, see priority 6). Needs a design decision (field shape, exact
secondary-marker count/placement/intensity) before implementation.

**Answered the same day (September 14, 2026), asked directly: how should
the climax timestamp be found for now?** The user wants both AI detection
*and* manual placement, not one or the other ("KI-Erkennung und manuelle
setzen") - and flagged that manual placement needs "a proper editor"
first, not a one-click button. This changes priority 6/7's status: they
now depend on the manual funscript editor (see "Later" below), which
was previously scoped as independent, deferred polish - it is now a
real prerequisite for the manual half of this feature, not just a nice-
to-have. AI detection (priority 6's own open design question: model,
signal shape, false-positive handling) remains separately open and
still needs its own design pass regardless of the editor.

**Manual half implemented (September 14, 2026):** rather than wait for
the full curve-point-dragging editor (still "Later", see below - a much
larger, less-scoped undertaking), the manual-placement half of this
priority turned out to be independently buildable on its own: mark a
region on the existing timeline (the drag gesture already used for the
Extended-O sidecar marker), then promote it to an O-marker with a
kind (primary/secondary) and, for secondary, an intensity - exactly the
"primary marker + optional weaker secondary markers" shape specified
above. Implemented as `funscript.OMarker`/`LoadOMarkers`/`SaveOMarkers`
(`funscript/omarker.go`) - written into the funscript's own `metadata`
field as `oMarkers`, per this section's own field-shape proposal, using
a raw-JSON merge rather than the typed `Script` struct specifically so a
field another tool (e.g. FunGen) may have added to the same file's
`metadata` is never silently dropped on save (proven by
`TestSaveOMarkersPreservesUnknownFields`, not just claimed) - and GUI
bindings (`GetOMarkers`/`SaveOMarkers` in `app_markers.go`) plus a
Player-tab panel (`playback.js`: add/list/remove, colored bands on the
existing heatmap/curve canvases, `omarker_test.py`). AI-assisted
placement (priority 6) still needs its own design pass and is not
started; this only closes the manual path.

**Classical auto-placement added (September 15, 2026), answering priority
6's open design question directly: asked the user "no separate system, but
detect as cleanly as possible" - and `funscript.SuggestOZone` (last eighth
of the script, highest-mean-position window) already served exactly that
purpose for the manual "O-Zone vorschlagen" button.** Extended it to
generation time: an opt-in "O-Marker automatisch vorschlagen" checkbox
(same pattern as the contact-vibration/AI-quality checkboxes) writes the
primary marker automatically when the signal is confident, nothing written
when it isn't (PR #52, `applyAutoOZoneMarker` in `app_generator.go`).

**Secondary markers implemented (PR #67, September 16, 2026), closing this
priority's last open piece.** The manual UI already supported adding a
secondary marker by hand (`playback.js`'s kind/intensity controls) - what
was missing was the suggestion algorithm. `funscript.SuggestSecondaryOZones`
extends `SuggestOZone`'s own approach: scans strictly before the primary
window for windows whose mean position stands out from their own local
baseline (the region's median, not a fixed number - a uniformly elevated
but flat lead-in isn't an "Erhebung") while staying clearly weaker than the
primary (below 85% of its mean). Up to two non-overlapping candidates,
`Intensity` derived from the actual mean-position ratio to the primary
marker (clamped 0.2-0.8) - "nicht so doll" comes from the measured signal,
not an invented constant. Wired into both existing entry points (the
playback tab's "O-Zone vorschlagen" button and the generation-time
auto-apply checkbox) - no frontend changes needed, since both already
render whatever `OMarker`s are present generically. A true content-based
climax *detector* (watching the video, not just the already-generated
position signal) remains unbuilt and would need its own design pass if
ever wanted - the classical signal-only approach above was judged
sufficient for now, and stays the whole of priority 6/7's answer.

### 8. Generator performance vs. FunGen2

The user's direction (September 14, 2026, explicitly prioritized this
session): generation is slower than FunGen2 and should use CPU/RAM/GPU
better regardless of which card is present. Per the repo's own standing
rule, profiled before touching anything.

**Baseline established (September 14, 2026, real clip, `cProfile`):** for
a single-ROI (`standard`/hub) run on the 256x144/42s real clip (2527
frames, no cache), total wall time was ~49s. `cv2.legacy.Tracker.update`
(CSRT) alone accounted for 44.4s - **~90% of total runtime**. Frame
decode (`cv2.VideoCapture.read`, 0.54s), camera-motion estimation
(`goodFeaturesToTrack` + `calcOpticalFlowPyrLK`, ~2.2s), and one-time
Python/scipy import overhead (~1.4s) are all minor by comparison. This
directly answers the open question from the previous version of this
note ("Python startup, frame decode, CSRT tracking, camera-motion
compensation, I/O?") - it's CSRT, overwhelmingly, not the other
candidates. Any optimization that doesn't touch CSRT's own cost is
chasing at most ~10% of total runtime.

**Tried and measured, reverted - a real negative result:** CSRT's
`update()` releases the GIL (confirmed: two trackers running in Python
threads instead of sequentially measured **1.68x faster in isolation**,
a tight loop on a static frame with no other work). For `tj`/`tf`
(two trackers per frame, fully independent - no shared state, no
inter-dependency), this looked like a clean, safe win, so it was
implemented (`concurrent.futures.ThreadPoolExecutor(max_workers=2)`
created once, both `tracker.update()` calls submitted per frame) and
verified correct (identical output to the sequential version: same
lost-frame count, same keyframes, same Quality Doctor score, all
existing tests green).

**But measured against the real end-to-end pipeline (not the isolated
microbenchmark), it gave no net benefit** - four timed runs (1200
frames each, alternating threaded/sequential to control for system-load
drift): threaded 34.4s/35.2s vs. sequential 33.0s/33.6s. The threaded
version was, if anything, marginally *slower*. Likely explanation: the
isolated benchmark only measured the tracker calls in a tight loop;
the real pipeline adds per-frame `ThreadPoolExecutor.submit()`/
`.result()` overhead (two of each, every frame, ~2500 times) plus frame
decode and Python bookkeeping in between, and that overhead was enough
to cancel out the parallel savings. Reverted rather than shipped -
this repo's standing rule is not to ship a change whose own measurement
doesn't support it. Ruled out: a persistent-thread-pool CSRT
parallelization of `track_two_points`, implemented exactly as described
above, is not a win as measured on this clip's resolution.

**Also tried, same session: a lower-overhead handoff - same negative
result, now confirmed robust rather than a `ThreadPoolExecutor`
artifact.** Two persistent worker threads synchronized with
`threading.Event` (submit a frame, wait for a "done" signal - no
`Future` object, no executor queue, the overhead this was meant to
rule out) gave the same result as the `ThreadPoolExecutor` version:
~33s for 1200 frames, same as sequential, no measurable win. Since two
independently-implemented parallelization strategies both failed to
reproduce the isolated benchmark's 1.68x in the real pipeline, the
cause is not implementation overhead in either approach - something
about the full pipeline (frame decode interleaved with tracking, real
per-frame memory allocation instead of a reused static buffer, or
OpenCV's own internal thread pool behaving differently under real
conditions than in a tight synthetic loop) evidently negates the
theoretical gain. Parallelizing the two `tj`/`tf` tracker calls is not
a productive lever at this clip's resolution; not worth another attempt
without a new hypothesis for *why* the isolated result doesn't transfer.

Not yet evaluated: whether a cheaper OpenCV tracker (KCF, MOSSE) gives
acceptable tracking quality for a real speed trade - CSRT was chosen
for accuracy, not speed, and this repo's own real-clip investigation
(priority 2, above) already shows tracking-quality tradeoffs need
measuring per clip, not assuming.

**Asked directly (September 14, 2026): why not just use more CPU/RAM to
go faster? Checked all three - CPU is already used, RAM doesn't apply,
GPU is a dead end for CSRT specifically:**

- **CPU is already heavily used per call, which is exactly why the
  threading attempts above didn't help.** Measured: a single CSRT
  `update()` call, with zero Python-level parallelism, already uses
  ~2.2 of this sandbox's 4 cores on average (`time.process_time()` /
  `time.time()` ratio over 300 calls) - OpenCV parallelizes the
  correlation-filter math internally (`cv2.getNumThreads()` defaults to
  `nproc`). That's why running two trackers "in parallel" at the Python
  level didn't add real capacity: each call was already using most of
  the available cores by itself, so two concurrent calls mostly
  contended for the same 4 cores instead of getting 2x the resources.
  On a real user machine with more physical cores, OpenCV's own internal
  parallelism should scale up automatically with no code change needed -
  not verified here (this sandbox only has 4 cores to test with), but a
  reasonable, evidence-backed expectation given how `cv2.getNumThreads()`
  works.
- **RAM doesn't apply to this bottleneck.** This is compute-bound (CPU
  cycles spent on correlation-filter matching), not memory-bound - more
  RAM only helps a workload that's swapping to disk from memory
  pressure, which a 256x144 video and two small tracker boxes never
  approach. Not a lever here.
- **GPU is a dead end for CSRT specifically, checked directly rather
  than assumed.** `cv2.getBuildInformation()` shows this environment's
  OpenCV has no CUDA support at all (every `cuda*` module listed
  "Unavailable") - consistent with `settings.js`'s existing hint that
  the usual pip OpenCV wheels aren't CUDA-built. But even a CUDA build
  wouldn't help here: OpenCV's own tracker API has no GPU-accelerated
  CSRT (`cv2.cuda` exists, `cv2.cuda.TrackerCSRT_create` does not) -
  only some other operations (e.g. optical flow, used by the flow
  backend and camera-motion estimation, not by CSRT) have CUDA
  variants, also unavailable in this build. A stronger GPU would not
  speed up the part of the pipeline that actually dominates runtime.

Net: none of "more CPU," "more RAM," or "more GPU" moves the needle
further here - the CPU is already close to saturated per call, RAM was
never the constraint, and GPU acceleration doesn't exist for this
specific algorithm in OpenCV. A real speedup needs either a different,
GPU-capable or cheaper algorithm (with its own quality trade to
measure, see above) or accepting CSRT's cost as roughly fixed per
frame.

**Asked again (same day): what about GPU for a preprocessing step
(sharpen/denoise before tracking) instead of for CSRT itself?**
Different question, different answer - unlike CSRT, filters like
`cv2.GaussianBlur`/unsharp masking or denoising do have CUDA-accelerated
OpenCV variants (`cv2.cuda.*`) in principle. Still needs a CUDA-enabled
OpenCV build, which this repo doesn't ship (see above) - packaging one
(matching the user's own NVIDIA card, "vielleicht keine größere aber
vielleicht später") is a separate, real effort (CUDA version matching,
a much larger wheel, platform-specific builds) worth doing only once a
preprocessing step is proven to actually help - see the unsharp-masking
result just above this section, which is promising but not yet proven
enough to justify that investment.

**Checked the flow-backend idea directly, same session - real, but
smaller and costlier than the existing docs claimed.** `--backend flow`
on the same real clip: 25.1s wall vs. the CSRT hub run's ~48s - a real
~1.9x speedup, not the "~4x" the code comment/GUI hint text claims
(that figure describes per-frame cost on a different, presumably
larger-ROI case; this clip's CSRT is unusually cheap at 256x144, so the
*relative* advantage of flow shrinks here). More importantly, **it cost
real quality on this clip**: Quality Doctor dropped from 1.00 (CSRT) to
0.55 ("PRÜFEN" - signal noisy rather than rhythmic, spectral
concentration only 16%), with a hint that the motion-center estimators
disagree - this is a busy, multi-subject scene (hands, two breasts,
the tip) where flow's automatic center-of-motion detection has more to
get confused by than a single clean tracked region does. So: steering
more users to flow-by-default is **not** the free win the existing
hint text implies, at least not for this kind of scene - it's a real
speed/quality tradeoff that depends on scene complexity, not a
strictly-better option. Worth keeping as an opt-in for simple,
single-subject scenes (where the existing hint text's premise likely
does hold) rather than promoting it as a general default.

**Closed the "not yet evaluated" item from above (September 15, 2026):
KCF and MOSSE were measured against CSRT on the same real clip, with the
same `quality_doctor` scoring used everywhere else in this section - real
speedup, but not shipped, because the quality cost is a cliff, not a
slope.** `cv2.legacy.TrackerKCF_create()` and
`cv2.legacy.TrackerMOSSE_create()` both exist in this environment's
OpenCV build and were swapped in for `track_roi()`'s tracker (both the
direct `cv2.legacy.TrackerCSRT_create()` call and the `create_tracker()`
reacquire path patch identically, since both resolve through the same
attribute), everything else - ROI, clip, camera compensation, scene-cut
handling, appearance memory, signal path - left untouched, run through
the real CLI end to end (`--report`, not a synthetic microbenchmark).

On the clip's `find_roi()`-suggested hub region (154x72px, the same kind
of "standard/hub" ROI the baseline above used) across the full 2527
frames: CSRT 88.2s / Score 0.90, KCF 17.6s (**~5.0x faster**) / Score
0.90, MOSSE 6.4s (**~13.8x faster**) / Score **1.00** - matching or
slightly beating CSRT's quality, not just close to it. A second,
order-alternated run at 1200 frames (to rule out system-load drift, same
method as the threading measurement above) reproduced the same ratios
(CSRT 42.8s, KCF 9.9s = 4.3x, MOSSE 3.2s = 13.5x) with all three at
Score 1.00. Taken alone, this would look like a clean win, better than
`flow`'s.

But the same swap on a small, deliberately hard 18x16px ROI (a tracked
tip region already on record, from the gentle-upscale investigation
above/priority 2, as the kind of target CSRT itself struggles with) tells
a different story. On the first 800 frames: CSRT itself already fails
this ROI (Score 0.40, "PRÜFEN" - noisy, a speed spike, 41% near-motionless
- but its own `update()` confidence never drops, so `tracker_lost_fraction`
reads 0.0 even while it's wrong). KCF and MOSSE do not degrade
gracefully alongside it - they **collapse**: KCF lost the object in 795
of 800 frames (99.4%), MOSSE in 798 of 800 (99.8%), both falling back to
"hold last known position" almost the entire clip - a functionally dead
signal, not a noisier one. Both finished in ~1.5s (49-56x "faster",
meaningless at that point) and both were also flagged `passed: false` by
Quality Doctor, so the failure doesn't slip through unnoticed - but the
mechanism of failure (near-total loss of lock, not gradual noise) is
qualitatively worse than what `flow` does on a hard scene.

**Verdict: not shipped, no production code touched.** This is a sharper
version of the same tradeoff `flow` presented, with one important
difference that rules out the same "opt-in for the right scene" answer:
`flow`'s cost is legible to a user before they commit to it (scene
complexity - "is this a busy, multi-subject shot?" - is something a
person can judge by watching the clip). KCF/MOSSE's cost is tied to ROI
size and target appearance stability in a way a user marking a box in the
GUI has no way to judge in advance, and the failure mode when it goes
wrong is a near-complete tracking collapse rather than a merely noisier
signal. Since this project's own selection of CSRT was explicitly for
accuracy over speed, and per the standing rule tracking-quality tradeoffs
need measuring per clip rather than assumed, offering KCF/MOSSE as a
silent opt-in would reintroduce exactly the unpredictable quality trap
the rule exists to avoid - a small/subtle ROI (common in this domain, per
the same "hands, two breasts, the tip" busy-scene note above) is a
realistic case, not an edge case. Revisit only if a cheap, reliable
"will this ROI track acceptably" pre-check becomes available (so the
choice could be made automatically rather than left to the user's guess),
not before.

**Follow-up (September 15, 2026), the user's own idea: a grid of Lucas-Kanade
points instead of one bounding box - built, measured, and this time SHIPPED
as an opt-in backend, because the measurement genuinely supports it.** The
reasoning behind the idea: CSRT/KCF/MOSSE all put "every egg in one basket" -
a single tracked box that either holds or is lost outright, which is exactly
why KCF/MOSSE collapsed above. `estimate_camera_motion()` in this same file
already tracks a scattered set of background points with
`cv2.calcOpticalFlowPyrLK` for camera-motion compensation; the idea was to
seed a grid of points *inside* the user's ROI instead, track each one
independently, and take the MEDIAN of the surviving points as the position
signal each frame - so a few lost points degrade the median a little instead
of collapsing the whole signal to a frozen position.

Built as `generator/grid_lk_backend.py` (`--backend grid_lk`), registered
through the same `backends.py` register used by `flow`/`two_point` - unlike
the KCF/MOSSE eval (which patched `create_tracker()` in-process without
touching production code first), this one earned its way into the tree only
after the numbers below came in. Re-seeding policy: every frame, points with
`status==0` are dropped; if the survivor count is below the target grid
size, fresh corners are pulled via `cv2.goodFeaturesToTrack`, masked to a
region around the CURRENT median position (not the whole frame - a
reacquisition anywhere in the image would be worthless). If ALL points are
lost in one frame, or a hard scene cut is detected (`detect_scene_cut`,
reused, not reimplemented), the entire grid is reseeded from scratch at the
last known position - the same idea as CSRT's own scene-cut reanchor in
`track_roi()`, just without an appearance memory (this backend has none).

Measured on the same real clip, same `quality_doctor` scoring, full CLI end
to end (`--report` methodology), a FRESH CSRT baseline re-established on
this session's hardware first (same discipline as the KCF/MOSSE eval, cross-
run absolute numbers aren't trusted) - and at least two grid densities per
ROI, as planned:

*Easy "hub" ROI (154x72px, same region as the KCF/MOSSE measurement, full
2527 frames):*

| Verfahren | Zeit | Speedup | Score | komplett verlorene Frames |
|---|---:|---:|---:|---:|
| CSRT (frisch gemessen) | 92.3s | 1x | 0.90 | - |
| Grid-LK 3x3 (9 Punkte) | 6.2s | 14.9x | 1.00 | 9/2527 (0.4%) |
| Grid-LK 4x4 (16 Punkte) | 5.9s | 15.6x | 0.90 | 9/2527 (0.4%) |
| Grid-LK 6x6 (36 Punkte) | 6.2s | 14.9x | 0.90 | 9/2527 (0.4%) |

Matches or beats CSRT's own quality at every density tested, at roughly the
same speed MOSSE reached on this ROI in the earlier measurement (~14x) -
without MOSSE's failure mode on the hard ROI, see below.

*Hard "tip" ROI (18x16px, same documented size as the ROI the KCF/MOSSE
collapse was measured on from the gentle-upscale investigation - the exact
pixel coordinates were never written down in this file, only the 18x16
size, so a same-size region was re-derived this session by eye from the
clip's first frame, tightly boxing the tip; CSRT's behaviour on it - Score
0.45 "PRÜFEN", 2 extreme speed spikes, spectral concentration 25%, on the
first 800 frames - lines up closely enough with the originally-documented
Score 0.40/"41% near-motionless" to treat as a fair stand-in, honestly noted
as a substitution rather than the exact original crop):*

800-Frame-Ausschnitt (dieselbe Fenstergröße wie die KCF/MOSSE-Messung):

| Verfahren | Zeit | Score | komplett verlorene Frames |
|---|---:|---:|---:|
| CSRT | 82.3s | 0.45 PRÜFEN | 0 (aber falsch - CSRTs eigene Konfidenz sinkt nicht) |
| Grid-LK 3x3/4x4/6x6 (alle drei) | 2.8-2.9s | 0.55 OK | 1/800 (0.1%) |

Voller Clip (2527 Frames) - hier zeigt sich der eigentliche Befund, den der
kurze Ausschnitt verdeckt:

| Verfahren | Zeit | Score | komplett verlorene Frames |
|---|---:|---:|---:|
| CSRT | 269.5s | 0.95 OK (1 Geschwindigkeitsspitze) | - |
| Grid-LK 3x3 (9 Punkte) | 6.0s | 0.64 PRÜFEN, **hard_fail** | 1498/2527 (59.3%) |
| Grid-LK 4x4 (16 Punkte) | 6.2s | 1.00 OK | 10/2527 (0.4%) |
| Grid-LK 6x6 (36 Punkte) | 6.8-7.6s | 1.00 OK | 10/2527 (0.4%) |

**Die Hypothese trägt - aber nur ab einer Mindestdichte, und das ist der
eigentlich interessante Befund.** Das 3x3-Gitter (9 Punkte) bricht auf
diesem Clip WIRKLICH zusammen, nicht nur "etwas mehr Rauschen": von den
1498 komplett verlorenen Frames liegen 1488 als EIN zusammenhängender Block
ab Frame 1039 - über die Hälfte des restlichen Clips am Stück, nicht
kurzzeitig, ohne Erholung bis Clip-Ende. Das ist qualitativ derselbe
Fehlermodus wie bei KCF/MOSSE (dauerhafter Verlust statt zunehmendem
Rauschen), nur nicht ganz so vollständig (59% statt 99%) - kein sauberer
Beleg für "graceful degradation", eher ein Beleg dafür, dass ein zu
kleines Gitter dieselbe Falle wie eine einzelne Bounding Box ist. Das
4x4-Gitter (16 Punkte) und das 6x6-Gitter (36 Punkte) zeigen dagegen GENAU
in diesem Abschnitt (Frame 1039-2527, wo 3x3 kollabiert) keinen einzigen
Aussetzer - beide bleiben durchgehend bei 0.4% verlorenen Frames, exakt
dieselbe Rate wie auf der leichten Hub-ROI. Das ist die tatsächliche
"graceful degradation": genug unabhängige Punkte, und der Median der
Überlebenden trägt durch eine schwierige Passage, die eine einzelne Box
(CSRT MIT Mühe, KCF/MOSSE gar nicht) nicht sauber übersteht.

Entscheidend für die Ausliefer-Entscheidung: der Mehrpreis für mehr Punkte
ist auf diesem Clip vernachlässigbar (6.0s bei 9 Punkten gegen 6.2-7.6s bei
16-36 Punkten - Frame-Dekodierung und Kamerakompensation dominieren die
Kosten, nicht die Punktzahl). Anders als bei KCF/MOSSE - wo es keinen Hebel
gab, das Kollaps-Risiko zu senken, ohne den ganzen Ansatz aufzugeben - gibt
es hier einen fast kostenlosen Hebel (mehr Punkte), der das gemessene
Kollaps-Risiko auf diesem Clip vollständig beseitigt hat.

**Verdikt: geshippt, als Opt-in.** `--backend grid_lk` ist jetzt verfügbar
(`generator/grid_lk_backend.py`, über das bestehende `backends.py`-Register
angemeldet wie `flow`/`two_point`, `csrt` bleibt Standard). Die Gitterdichte
ist FEST auf 6x6 (36 Punkte) codiert und bewusst NICHT als CLI-Option
freigegeben - eine ungeeignete Dichte zu wählen wäre genau die unsichtbare
Falle, die bei KCF/MOSSE zum Nicht-Ausliefern führte, nur dass es hier
einen kostenlosen sicheren Ausweg gibt (einfach immer die höhere Dichte
nehmen) statt eines Nutzer-Ratespiels. Ehrlich zu benennen bleibt: das ist
Evidenz von einem Clip und einer (nachgebildeten, nicht exakt
identischen) ROI - derselbe evidenzielle Standard, auf dem auch `flow`
seinerzeit als Opt-in geshippt wurde, keine stärkere Garantie. Getestet:
volle `generator/*_test.py`-Suite (23 Dateien, alle grün) plus neue
`generator/grid_lk_backend_test.py` (Vertragsform, Amplitude, graceful
degradation bei teilweise texturloser ROI, Szenenschnitt-Erholung,
Kamerakompensation, Achsenwahl, `max_frames`) sowie `backends_test.py`
unverändert grün. Kein GUI-Wiring in diesem Durchgang (nur die CLI-Option,
wie es die Aufgabe verlangte) - ein Kontrollkästchen analog zu
`#gen-flow` in `generator.js` wäre ein naheliegender nächster Schritt,
nicht Teil dieser Änderung.

**Korrektur (16. September 2026, beim GUI-Audit für #66 gefunden):** dieser
nächste Schritt war zu diesem Zeitpunkt bereits erledigt - `#gen-backend`
ist seit einer früheren Änderung ein Dropdown mit allen drei Verfahren
(`csrt`/`flow`/`grid_lk`), nicht nur eine `#gen-flow`-Checkbox. Der obige
Absatz blieb bis heute unkorrigiert im Dokument stehen, obwohl der
beschriebene nächste Schritt längst erledigt war.

**Follow-up (September 15, 2026), user's question: does the grid-of-points
idea also help Tf/Tj's two-point distance measurement, not just single-ROI
tracking? Found a real bug on the way, then a real negative result.**

**Bug found first: `--backend` was silently ignored whenever `--roi2` was
set.** `generate_funscript.py`'s dispatch routed straight to
`track_two_points()` (hardcoded `create_tracker()`, i.e. always CSRT) any
time `--roi2` was given, regardless of `--backend` - the exact same "silent
backend drop" class this section's own grid_lk GUI-wiring commit (#46)
already fixed once for the single-ROI path, just not caught here. Verified
directly: two runs with `--backend csrt` and `--backend grid_lk`, same
`--roi`/`--roi2`, produced byte-identical output files. Fixed:
`grid_lk_backend.py`'s tracking loop was split into `_track_grid()` (shared
core, raw uncompensated x/y per frame) with `analyze()` (single-ROI, unchanged
public contract - `grid_lk_backend_test.py` still passes byte-for-byte)
and a new `analyze_two_point()` (two independent grids, one per region, full
2D distance between their medians - mirrors `track_two_points()`'s own
contract and reasoning, deliberately skips camera compensation for the same
reason `track_two_points()` does: a common pan cancels in the distance by
construction, applying two independent per-region estimates would only add
uncorrelated noise). `generate_funscript.py` now branches on `--backend
grid_lk` before falling through to the CSRT path. New test:
`grid_lk_two_point_test.py`, same synthetic pan+common-motion+real-signal
video as `two_point_test.py`, added to CI's video-test job.

**Now that grid_lk genuinely runs in two-point mode, measured against the
real 42s clip's FunGen `tj` reference (same ROI1 tip `115,42,22,28` as the
priority-2 ablation above, two ROI2 anchors already on record there):**

| ROI2 anchor | backend | own quality | lost frames | time | r vs FunGen (best) | r at lag=0 |
|---|---|---:|---:|---:|---:|---:|
| neck `110,5,35,22` | grid_lk | 0.90 | 10/2527 (0.4%) | 3.7s | 0.070 (boundary) | -0.045 |
| lower cleavage `110,92,35,25` | grid_lk | 1.00 | 9/2527 (0.4%) | 3.8s | 0.077 (boundary) | 0.030 |
| neck `110,5,35,22` | csrt (fresh) | 0.80 | 222/2527 (9%) | 95s | **0.524**, inverted, n=422 | 0.524 (not boundary) |

**Grid_lk is not a win for two-point tj tracking - if anything the opposite,
the same "own quality ≠ FunGen match" pattern already on record twice in
this document.** Both grid_lk runs have far better own-quality numbers
(0.90-1.00 score, <1% lost frames, ~25x faster) than CSRT, and both still
score near zero against the FunGen reference, pinned to the lag-search
boundary even at that - the same "coincidental match on a shrinking overlap"
warning sign this document's own lag-search-window fix exists to catch, i.e.
not a trustworthy match at all. Reading: grid_lk's per-region median is a
good ROBUSTNESS improvement (rarely loses lock) but that robustness doesn't
translate into agreeing with FunGen's judgment of where the real motion is -
consistent with the standing finding that own-quality metrics and FunGen
agreement are largely independent axes. Not shipped as a `tj`/`tf` default;
`--backend grid_lk --roi2` stays available (now genuinely, not silently
ignored) but the CLI help text was updated to say plainly that it measured
worse here.

**The fresh CSRT number is the more interesting result, with an honest
caveat.** r=0.524 at lag=0 (not boundary-pinned, full n=422 overlap,
independently re-verified by hand outside `fungen_compare.py` too) is the
best tj correlation recorded anywhere in this document - clearly better
than the two older hand-tuned runs (0.336/0.356) and far better than this
same ROI pair's own previously documented figure of r=0.123. Both figures
are real measurements, not reconciled: the ROI1/ROI2 pixel values match
exactly what's on record, but the exact original CLI invocation (this run
used `--adaptive-keyframes 6`; unclear if the original did) wasn't
preserved verbatim, so the discrepancy's cause isn't established - flagged
rather than silently picking one number. Worth a deliberate re-run with a
pinned, written-down command before trusting r=0.524 as the new baseline
for this ROI pair.

### 9. SAM: long-term architecture direction — first milestone scoped

The user's direction (September 14, 2026): a 10-phase vision document for
evolving toward a richer internal motion model ("SAM"), with `.funscript`
kept as a compatibility layer rather than the primary internal format.
Discussed and scoped the same day - full detail, the existing-component
mapping, what's explicitly deferred (webcam/live input, per the user's own
"vielleicht für ein neues eigenes Produkt"), and the concrete first
milestone (SAM Motion Model, SAM Script v0.1, bidirectional funscript
converter) are all in **`docs/SAM_ARCHITECTURE.md`** - not duplicated here.

**First milestone shipped** (`sam/` package: `Script`/`Frame`/`Motion`
types, `Load`/`Parse`/`Save`, `FromFunscript`/`ToFunscript`), but not
wired into any GUI/CLI flow - nothing produces or consumes `.sam` files
yet, on purpose (see below).

**Tested with real project data (September 15, 2026), at the user's
request** ("die Daten die wir schon haben einfließen lassen, gerade im
Bezug auf die Hardware"): ran an actual tj-profiled `.funscript` this
session already generated from the real clip (`--profile tj
--contact-vibration`, `sam/funscript_test.go`'s
`TestRoundtripPreservesProfileAndDeviceRecipe` embeds a real excerpt)
through a full file roundtrip - `FromFunscript` → `Script.Save()` → disk →
`sam.Load()` → `ToFunscript()` - and diffed against the original.

**Found a real bug this way, not a hypothetical one:** `Profile` and
`DeviceRecipe` were never carried into `sam.Metadata` at all - a Tf/Tj
script roundtripped through SAM would silently lose `profile: "tj"` and
its `device_recipe` (including `contact_vibration`), so
`funscript.IsDistanceProfile()` would read false afterward and playback
would fall back to the Hub-style position mapping on real hardware
instead of the Abstand/Sog one - no error, just the wrong actuator
behavior. Also found `ToFunscript` truncated `Motion.Position` to an int
instead of rounding (harmless today since the only producer always writes
whole numbers, but would bias any future fractional-position producer
downward). Both fixed: `sam.Metadata` now carries `Profile` and a
`sam.DeviceRecipe` (own type, not `funscript.DeviceRecipe`, to keep this
package independent of `funscript`'s shape - see package docstring),
`ToFunscript`/`FromFunscript` convert both ways, rounding replaces
truncation. Verified with the same real clip's data: actions, profile,
and device recipe all identical after the full file roundtrip, and the
resulting `.funscript` loads correctly through
`fungen_compare.py::load_actions` - so a SAM-roundtripped script stays as
comparable against FunGen references as any other output.

**Still correctly not a new on-disk Funscript format:** `.funscript` stays
the user-facing file. SAM is the internal motion model; Enrich +
`PlaybackFramesFromFunscript` now sit between load and device output for
Tf/Tj contact (Intensity/Gaps). Optional `.sam` via CLI is tooling only.

**First Enrich + playback consumer (September 17, 2026):**
`sam.FromFunscriptEnriched` fills Velocity/Confidence/Intensity/Range;
GUI playback for contact uses `sam.PlaybackFramesFromFunscript` (log:
„Kontakt-Vibration aktiv (SAM)“). CLI: `SamNPlayer sam
script.funscript`.

**SAM runtime (milestone 2 start, September 17, 2026):**
- `sam.Densify` — tick-grid Intensity from interpolated Position (classic
  parity; no keyframe-lerp pre-buzz).
- `sam.RuntimeAdjust` — live IntensityScale / Mute / ExtraSmooth without
  rewriting `.funscript`/`.sam`.
- CLI playback for Tf/Tj+contact uses SAM path; flags
  `--contact-intensity`, `--mute-contact`, `--contact-extra-smooth`,
  `--contact-span`, `--contact-curve`.
- GUI: Kontakt-Stärke / Empfindlichkeit / Kurve live; Vibrationsspur folgt
  (`GetVibrationCurvePreview`).

Guiding constraint from the same conversation, worth restating because it
governs every step of this: improve, never regress or dilute what already
works, and go about it "professionell, nicht auf Teufel komm raus testen" -
the same measured, evidence-first discipline already used for the CSRT/
FunGen and performance investigations above (priorities 2 and 8).

### 10. Player: sharper video display — investigated, not shipped

The user's direction (September 15, 2026): the Player tab's `<video>` is
stretched via CSS (`width:100%`, see `style.css`) - for low-resolution
source clips this can mean a real, visible upscale (e.g. this session's
256x144 test clip shown at ~900px wide, a ~3.5x stretch) using only the
browser's plain bilinear scaling, no sharpening. Asked whether a real
upscaling/sharpening improvement could be built into the Player.

**Built, then measured, then reverted - a real, if unwelcome, finding.**
A CSS-only fix (a sharpen filter, `image-rendering` tricks) can't work:
CSS has no unsharp-mask filter, and `image-rendering: crisp-edges` trades
smoothness for blockiness, which is worse for photographic video. The
only technique that can genuinely add detail beyond plain bilinear
upscaling at video framerate is a WebGL shader pass, so that's what was
built: a `<canvas>` alongside the `<video>`, an opt-in checkbox, a
Laplacian/unsharp-mask fragment shader (`center*4 - sum(4 neighbors)`,
added back scaled by an adjustable amount), each frame uploaded via
`texImage2D(video)` and rendered with `object-fit:contain`-equivalent
letterboxing to match `<video>`'s own sizing behavior exactly (an
earlier version of this that skipped the letterboxing step visibly
distorted the image - caught by the same verification, not shipped
separately).

**Verified before shipping, per this session's own standing rule, using
the real test clip transcoded to VP9/WebM** (this environment's headless
Chromium build has no H.264 decode) **and an objective sharpness metric
(Laplacian variance) on same-size, same-position screenshots, not a
subjective look:**

| rendering path | Laplacian variance (higher = sharper) |
|---|---:|
| native `<video>`, plain CSS bilinear upscale | 331 (fixed-crop measurement) |
| WebGL canvas, shader amount=0 (no sharpening) | 126 |
| WebGL canvas, shader amount=0.8 | 154 |
| WebGL canvas, shader amount=1.5 (top of the allowed range) | 203 |

The sharpening formula itself works as designed - variance rises
monotonically with the amount slider, confirming the shader compiles,
links, and runs correctly (also checked directly: zero shader
compile/link errors in the console). **But the baseline is already far
below plain video before any sharpening is applied at all**, and even
the strongest tested setting doesn't close the gap. Isolated further to
find out why: a plain **Canvas 2D `drawImage` of the same video frame**
(no WebGL, no shader) scored even lower (5.3) - so the loss isn't in the
shader or in WebGL, it happens the moment a video frame is pulled out of
`<video>` into any canvas at all. A **control test with a static PNG**
(same clip, one exported frame) showed canvas `drawImage` and CSS-scaled
`<img>` scoring near-identically (6.44 vs 6.45) - so generic canvas
scaling is not the problem either. The loss is specific to *video frame
extraction* (`texImage2D`/`drawImage` from a live `<video>` element),
not to canvas rendering or to this particular shader.

**Not shipped, changes fully reverted** (`playback.js`, the new
`sharpen_test.py`) - shipping a feature billed as "sharper" while having
direct, measured evidence it currently makes the image measurably
*less* sharp than doing nothing would be shipping a placebo at best, a
regression at worst, exactly what "professionell, nicht auf Teufel komm
raus" rules out. Honest uncertainty about *why*, worth recording instead
of guessing: this might be a genuine, general result (video decoding
platforms commonly use a specialized compositor/scaling path for live
`<video>` display, separate from generic frame-extraction APIs, so this
could reproduce on real hardware too) or it might be an artifact of this
specific environment (headless Chromium, software VP9 decode via
SwiftShader, no real GPU) - there is no way to tell apart from here, and
that uncertainty is the honest reason this isn't shipped even as an
opt-in, off-by-default toggle: an "improvement" that might just be
broken everywhere isn't worth exposing to the user at all until it's
been tried on real hardware.

If revisited later: test on a real machine with real GPU/video decode
first, before writing any shader code - if the same frame-extraction
loss reproduces there, this whole approach (canvas/WebGL video
post-processing) is a dead end and not worth attempting again; if it
does *not* reproduce, the already-built shader and letterboxing logic
above are a reasonable starting point to resurrect from this entry's
git history rather than rebuilding from scratch.

### Later

- **New this session (September 15-16, 2026), not previously listed here:**
  bootstrap/export tooling to train a custom ROI YOLO model for `ai_roi.py`
  (PRs #64/#65/#66, see `docs/AI_ADAPTER.md`'s "Still open: no
  bundled/recommended ONNX model" section for the full writeup) - no
  trained/bundled model shipped, this only builds the tooling; a classical
  audio-tempo plausibility check (`--audio-check`, `generator/
  audio_check.py`) fully wired into the GUI alongside the existing
  AI-quality-opinion checkbox.
- ~~Script Doctor for imported `.funscript` files.~~ **Done (PR #57,
  September 15, 2026):** reuses Quality Doctor's own actions-only fallback
  path (never exercised before this, since the generator's own call site
  always supplied full tracking data) via a new `--script-quality` CLI
  flag and a "Skript prüfen" button in the playback tab, explicitly marked
  `estimatedFromScriptOnly` so it's never confused with a post-generation run.
- ~~A manual funscript editor (edit/drag individual points on the curve)
  with the video alongside it~~ **Done:** the playback curve is now a
  point editor (drag to move, click to add, double-click to delete), and
  the video follows the point being dragged during a drag - closing this
  and unblocking priority 7's manual O-marker path.
- ~~Training history across multiple sessions.~~ **Done (PR #55,
  September 15, 2026):** sessions were already logged as JSONL
  (`app_training.go`'s `openSessionLog`) but never read back - a "Verlauf"
  section in the training tab now summarizes past sessions (cycles, mean
  peak intensity, interruptions, mean feedback) from those same logs.
- ~~AI as a replaceable analysis backend... GUI wiring for all three
  remains open~~ **Stale as of September 14, 2026, corrected while
  auditing what's still missing from the GUI:** this was true when
  written, no longer is. All three are fully wired end-to-end in
  `generator.js` - region proposal (`#gen-ai-roi` checkbox, greyed out
  via `CheckAIRoiAvailable` when no local ONNX model is available,
  `AutoDetectROI(video, 'ai')` when checked), profile proposal
  (`SuggestProfile`, used by the "Profil vorschlagen" flow), and quality
  second-opinion (`#gen-ai-quality` checkbox -> `aiQualityOpinion` in
  `GenerateOptions` -> `aiOpinionVerdict`/`aiOpinionReason` shown in the
  result). See `docs/AI_ADAPTER.md` for the underlying architecture,
  still accurate. Reusing raw data/parameters/quality reports/confirmed
  ratings as actual training input (as opposed to just proposing and
  measuring, which already works) is the part that's genuinely still
  open - see `quality_model.py`'s existing pattern and the Training Lab-
  adjacent asks elsewhere in this document.
- **The user's direction (September 14, 2026), noted for later, not
  decided:** whether to stay with Go + Python long-term, and what "our
  own AI" should mean. Discussed, not started:
  - Language: no case to switch the Go shell (app/device/GUI backend) to
    anything else. The actual complaint is that Python is an *external*
    install, not that it's the wrong language for CV work. Two ways to
    fix that without a rewrite: bundle a portable/frozen Python runtime
    with the app (removes the separate `pip install` step, keeps all
    current generator code); or, bigger and later, port the classical
    tracker to Go (`gocv`) and drop Python entirely. Prefer the first if
    "no separate install" is the actual goal.
  - "Our own AI": `quality_model.py` already *is* this pattern - it
    trains a small model from the user's own accept/reject judgments and
    only adopts it if it beats the fixed rules in cross-validation, no
    external dataset needed. Extending that same approach to profile
    suggestion and ROI refinement (learn from accumulated tracking
    signals + confirmed ratings) fits "specific to our system" better
    than adopting a generic pretrained detector, and doesn't need a
    large labeled dataset to start from.
- ~~Reusing raw data/parameters/quality reports/confirmed ratings as
  actual training input... is the part that's genuinely still open~~
  **Partially done (September 16, 2026):** a "KI-Trainingssystem" GUI tab
  now wraps `bootstrap_yolo_dataset.py`/`train_yolo_model.py` end-to-end -
  mark one or two regions in a video run through the app, classical
  tracking labels frames automatically, a review grid lets wrongly-tracked
  samples be discarded before training, and "Training starten" runs the
  real `ultralytics` training + ONNX export locally (GPU required for
  realistic times), dropping the model straight at the AI model path. This
  closes the "collect labeled data from real usage" half of the gap for
  the YOLO region detector specifically; `quality_model.py`-style
  continuous learning from accept/reject judgments for profile/ROI
  refinement (the other half of that paragraph) is still open.
- **Measured, real-clip finding (September 16, 2026):** on a real titjob
  POV clip, Tf/Tj (two-point distance) scored r≈0.055-0.095 against a
  FunGen reference regardless of backend (CSRT/grid_lk) or ROI-selection
  quality, while single-region Standard mode scored r≈0.178-0.263 (2-4x
  better) on the *same* clip with the *same* backends. Ruled out via
  direct experiment, not guessed: bad backend choice (tried CSRT, grid_lk,
  flow - none closed the gap), bad ROI selection (auto vs. hand-picked
  made no measurable difference), and coarse aggregation (extended
  grid_lk to a mesh-of-points minimum-pairwise-distance signal instead of
  centroid-distance - still no improvement). Contact-vibration inherits
  this same unreliability, since it only activates under Tf/Tj's distance
  signal (`funscript/mapper.go`'s `contactEnabled`). Conclusion: the
  remaining gap needs class-aware detection (the FunGen2 screenshot the
  user shared shows trained "breast"/"hand"/"penis" object classes, not a
  motion heuristic), which is exactly what the KI-Trainingssystem above
  now builds toward - motion-only two-point tracking has been tried from
  several angles and does not close it further.
- Also fixed the same day: `--axis` used to be a fixed manual choice
  (`x` or `y`) even though every backend already tracks both axes for
  free (kept for the "is there any horizontal motion at all" hint). Now
  defaults to `auto`, picking whichever axis has the clearly larger
  range - see CHANGELOG.md. Unrelated to the Tf/Tj finding above (that's
  single- vs. two-region tracking; this is single-region axis choice) but
  found via the same round of real-clip testing.
- **`region_fusion` backend added and measured (September 16, 2026)**,
  the user's "Gitter + Abtastung" (grid + sampling) proposal from the
  same conversation: split the marked region into a 2x2 grid (4
  sub-regions), track each independently (same `_seed_grid`/`_reseed`
  point-tracking primitives as `grid_lk`), and fuse them PER FRAME into
  one signal weighted by each sub-region's own EMA-smoothed motion
  strength - the idea being that a single box/grid spanning the whole
  region implicitly averages in whatever part is currently still,
  diluting real motion confined to one part of it. Blending absolute
  positions (not switching to a single "winning" region) is deliberate:
  the four sub-regions all sit inside the already-localized, user-marked
  ROI, not spread across the whole frame, so a weighted average of their
  positions stays physically meaningful - no jump between distant image
  areas to smooth over.

  Synthetic test (`region_fusion_backend_test.py`) confirms the core
  mechanism: when motion is confined to one of the four sub-regions, the
  fused output keeps most of the true amplitude (`np.ptp(pos_quad) >
  quad_amplitude/4 * 2`) instead of being diluted toward a naive
  four-way average, and `mean_weight_spread` (how unevenly the fusion
  weighted the sub-regions) comes out clearly above 0 in that case,
  confirming the weighting actually engaged rather than just defaulting
  to a uniform average.

  GEMESSEN on real material - a newly uploaded reference for a different
  clip ("Fucking a MILF...", FunGen 2.6.3 run WITH ITS OWN YOLO DISABLED,
  i.e. FunGen2's own classical fallback, not its AI mode - a fairer
  apples-to-apples comparison than the Tf/Tj measurement above, which
  compared our classical tracking against FunGen2's full AI pipeline).
  Two 90-second segments, same ROI, `csrt`/`grid_lk`/`region_fusion` all
  run and correlated against the same reference window
  (`fungen_compare.best_lag_correlation`, ±2s lag search):

  | segment (clip time) | csrt | grid_lk | region_fusion |
  |---|---|---|---|
  | 580-670s | r=0.250 | r=0.130 | r=0.251 |
  | 880-970s | r=0.286 | r=0.335 | r=0.368 |

  region_fusion was at least on par with csrt in both segments and
  clearly ahead of both csrt and grid_lk in the second - a real,
  measured (not assumed) improvement, though modest, not a dramatic
  closing of the gap to FunGen2's numbers seen elsewhere in this
  document.

  IMPORTANT CAVEAT, found while running this comparison: the reference
  funscript's total duration (~1519s) and the uploaded video's duration
  (~1573.5s) differ by ~54 seconds, and widening the lag search well
  past ±2s (`max_lag_ms=10000`) still pushed the best-fit lag to the
  search boundary on segment 2 instead of settling - meaning clip and
  reference are not a precise 1:1 time match (different source cut,
  or FunGen2 skipped a chapter its own logic considered non-sexual).
  The r-values above should be read as directional, not exact - a
  precisely time-aligned comparison (matching source cut, or deriving
  the true offset e.g. via audio cross-correlation) would be needed
  before treating region_fusion as conclusively better than csrt rather
  than "at least competitive, sometimes clearly better." Also observed
  independent of backend choice: this specific clip triggers `--axis
  auto`'s scene-cut detector on roughly 10% of frames in both tested
  segments - unusually high, and shared by all three backends equally
  (so not a backend quality difference), worth a separate look if it
  turns out to be a genuine over-triggering issue rather than actually
  cut-heavy source material.
- **`region_fusion_auto` backend added (September 16, 2026)** - the user
  clarified after the above that they hadn't meant subdividing a
  hand-marked region: "ich meinte nicht zwei Stellen markieren sondern das
  Bild des Videos automatisch immer in 4 Zonen teilen und dann mehrere
  Punkte verteilen" (I didn't mean marking two spots, but automatically
  always dividing the video image into 4 zones and distributing several
  points). `region_fusion` (above) does need a marked region; this is the
  actually-requested no-marking variant, analogous to how `flow` needs no
  ROI.

  NOT simply `region_fusion_backend.analyze()` called with a full-frame
  ROI - that would blend the four zones' ABSOLUTE pixel positions, which
  `region_fusion`'s own module comment explicitly justifies only because
  its four sub-regions sit inside an already-small, localized marked ROI.
  Four zones covering the whole frame sit at opposite corners; blending
  their raw pixel coordinates would produce a meaningless jump across the
  image whenever the activity weighting shifts from one zone to another.
  Caught this before shipping by re-reading `region_fusion_backend.py`'s
  own rationale rather than assuming the same code would generalize.

  Fix: each zone's tracked position is normalized to its OWN box (0..1,
  top/left to bottom/right of that zone) before fusing - comparable across
  zones regardless of where on screen the zone sits - then the
  activity-weighted fusion scales back to a pixel-like range (×frame
  height/width) for downstream stats. New file
  `region_fusion_auto_backend.py` (not an edit to `region_fusion_backend.py`,
  to avoid any risk to its already-measured, shipped behavior above).

  Camera compensation runs PER ZONE, not once for the whole frame: the
  existing `estimate_camera_motion` needs background outside the tracked
  area to sample from, and a single "region" spanning the whole frame
  would leave none. Per zone, the other three zones (75% of the frame)
  remain as background, so it still works - at roughly 4x the camera-comp
  cost of `region_fusion`.

  Point density had to be raised from `region_fusion`'s 3x3 per sub-region
  to 6x6 (same as `grid_lk`'s whole-ROI density) after the synthetic test
  first failed: a zone here is a quarter of the WHOLE frame (often
  hundreds of pixels), not a quarter of an already-tight marked ROI, so a
  3x3 grid left gaps wide enough for a moderately-sized moving object to
  fall between every seed point and register ~0px of measured motion (a
  320x240 test video, 160x120 zone, 15px object: 0.1px measured instead of
  the true 30px). 6x6 fixed it in the same test.

  Synthetic test (`region_fusion_auto_backend_test.py`) confirms motion
  confined to one screen corner produces a real, bounded signal - clearly
  above what the previous (too-sparse) grid measured, and clearly below a
  frame-spanning jump, i.e. neither of the two failure modes above.

  NOT YET measured against a FunGen2 reference (unlike `region_fusion`
  above) - a candidate, not a result. Worth checking specifically whether
  the per-zone normalization trades away some real amplitude information
  compared to `flow`'s whole-frame dense approach before recommending it
  over `flow` for no-ROI use.

- **Go-native tracking, first step: `generator/trackcv` (September 16,
  2026)** - the user's standing complaint about Python's install burden
  ("man muss so viel nach installieren", comparing unfavorably to FunGen2's
  move to C#) came up again; asked directly whether to start moving parts
  of the generator to Go, on one condition: only where it's a measured
  win, not a rewrite for its own sake ("ich will das wir mehr selber
  schreiben als irgend was nutzen was dann kein gute Ergebnis bringt" - I
  don't want us writing more ourselves than using something that then
  gives a worse result).

  FIRST MEASURED BEFORE WRITING THE REAL PORT: a throwaway feasibility
  test - a ~60-line cgo wrapper around `cv::TrackerCSRT`, the SAME
  synthetic test video and ROI fed to both it and Python's
  `create_tracker()`. Result: r=0.9996 Pearson correlation, max. 3px
  absolute difference, mean 0.48px (both trackers ran on a 200-frame
  clip; the small per-frame differences are ordinary floating-point/
  threading nondeterminism inside CSRT itself, not a binding quality
  issue). Amplitude: Go 77.0px vs. Python 78.0px against a true 80px.
  Performance on a larger 640x480/600-frame clip: Go ~13.6s wall time,
  Python ~16.0s (both include video decode) - ~15% faster, from the
  removed per-frame Python/ctypes marshalling overhead; the actual CSRT
  computation is identical C++ code either way, so this is NOT a
  multiplicative "Go is N times faster" story - most of the cost is the
  algorithm itself, not the language wrapping it.

  WHY NOT `gocv`: the obvious first choice, but its `contrib` Go package
  (where `TrackerCSRT` lives in every gocv version checked, v0.30.0
  through v0.43.0) bundles ALL contrib bindings - including
  `xfeatures2d.cpp` - into one cgo compilation unit. Ubuntu's
  `libopencv-contrib-dev` doesn't ship `xfeatures2d` (SIFT/SURF, excluded
  for patent reasons), so the whole package fails to compile with "fatal
  error: opencv2/xfeatures2d.hpp: No such file or directory" even though
  only the unrelated tracker binding was needed. Newer gocv (v0.43.0)
  separately failed for a different reason: its `aruco.cpp` targets a
  newer OpenCV `ArucoDetectorParameters` API than Ubuntu's packaged 4.6.0
  provides. Rather than pin to some in-between gocv version and hope both
  problems stay avoided, `generator/trackcv/cv.h`+`cv.cpp` binds only the
  ~10 OpenCV functions actually needed (`VideoCapture`, `TrackerCSRT`,
  `goodFeaturesToTrack`+`calcOpticalFlowPyrLK`+`estimateAffinePartial2D`
  for camera compensation, `calcHist`+`compareHist` and a resized-frame
  mean-diff for scene-cut detection, `matchTemplate` for appearance
  memory) - no unrelated contrib modules dragged in.

  THE REAL PORT (`track.go`, `appearance_memory.go`, `savgol.go`): a
  frame-for-frame port of `track_roi()` - same scene-cut thresholds
  (histogram correlation < 0.5 OR resized-frame mean-diff > 12.0), same
  appearance-memory behavior (remember every 25 frames, keep 8 templates,
  index 0 - the user-confirmed start region - never evicted, 0.55 minimum
  match score to reacquire), same segment-wise Savitzky-Golay smoothing
  of the cumulative camera-shift signal (window 9, order 2) before
  subtracting it from the position curve. The Savitzky-Golay filter has
  no Go stdlib/small-dependency equivalent, so it's implemented directly
  (a tiny per-point least-squares polynomial fit, window shifted rather
  than shrunk or mirrored at the array edges - matching
  `scipy.signal.savgol_filter`'s default `mode="interp"`, not the more
  common "mirror at the edge" behavior another implementation might
  reach for).

  TWO REAL BUGS CAUGHT BY THE TEST SUITE, not by inspection:
  1. `Gray_HistCorrelation` passed a bare `0` where `cv::calcHist` expects
     a `const int*` channels array - undefined behavior that happened to
     not crash in isolation but corrupted the heap enough to abort much
     later, inside an unrelated `VideoCapture` close call ("corrupted
     double-linked list" / SIGABRT) - a textbook case of a memory bug
     surfacing far from its actual cause. Fixed by passing a real
     `int channels[] = {0}`.
  2. A tracker double-free: `defer tracker.Close()` right after creating
     the tracker captures the POINTER VALUE at the `defer` statement, not
     at the deferred call - but the scene-cut path reassigns `tracker` to
     a fresh one on every cut (closing the old one explicitly first).
     The deferred call still held the very first tracker, so function
     return closed it a second time ("double free or corruption").
     Fixed by deferring a closure (`defer func() { tracker.Close() }()`)
     that reads the current value of `tracker` when it actually runs.
     Both bugs only manifested with `SceneCutDetection`/`AppearanceMemory`
     enabled - exactly the one test exercising those paths (the other
     four passed cleanly, which is itself a reminder that a green build
     with narrow option coverage proves less than it looks like it does).

  OWN TEST SUITE, no cross-language dependency: `track_test.go` generates
  its own synthetic videos via the package's own `VideoWriter` binding
  (no ffmpeg, no Python) - matching the "Go"/"Python (Generator)" CI
  split already in place, and the same "known ground truth, measure
  against it" pattern the Python backend tests use (`*_backend_test.py`).
  Checks: contract shape, amplitude accuracy on clean material, camera
  compensation measurably improving amplitude accuracy on a panning
  clip, scene-cut detection + recovery, axis selection, `max_frames`.
  Passes clean under `go test -race`, stable across repeated runs (no
  flakiness left over from the two bugs above).

  NOT YET the default or even reachable from the app: nothing imports
  `generator/trackcv` - `generator.go` still shells out to
  `generate_funscript.py` for the entire pipeline (tracking AND
  smoothing AND keyframe extraction AND Quality Doctor AND funscript
  writing). Wiring it in for real raises a bigger, still-open question
  this entry deliberately does NOT answer: the Python pipeline downstream
  of tracking (savgol/find_peaks-equivalent smoothing, keyframe
  extraction, the Quality Doctor heuristics, funscript I/O) would either
  need its own Go port - real additional work, since those aren't as
  cleanly separable as the tracking loop and have no obvious Go
  numerical-library equivalent to scipy - or Go's tracker would need to
  hand its output BACK to Python for the rest, which keeps Python as a
  hard runtime requirement and gets none of the "avoid installing
  Python" benefit the user's original complaint was actually about (only
  the "wenn es Performance bringt" half of their stated condition, not
  the underlying motivation). That tradeoff needs a decision before the
  next piece is picked, not a silent default.

  CI: `.github/workflows/tests.yml`'s `go` job and
  `.github/workflows/release.yml`'s dependency step both now install
  `libopencv-dev`/`pkg-config` - without it, `go vet`/`go test ./...`
  (which reaches `generator/trackcv` even though nothing imports it,
  since Go tests every matched package) fails with "opencv2/opencv.hpp:
  No such file". The Windows cross-build is unaffected: nothing in
  `cmd/gui-wails`'s or `cmd/cli`'s import graph reaches
  `generator/trackcv` yet, so Go's build simply never touches it there -
  worth re-checking the moment something DOES import it, since cross-
  compiling cgo against a Windows OpenCV build is a real, separate
  problem this milestone hasn't had to solve.

- **Go-native post-tracking + opt-in pipeline (September 16, 2026)** —
  the open question above was answered explicitly: the goal is **more Go
  / less Python** (not "bundle a frozen Python"), so the next piece is
  the downstream signal path, not only wiring trackcv into a Python
  remainder.

  `generator/posttrack` ports `positions_to_funscript` and `limit_speed`
  to pure Go (no cgo, no OpenCV): Savitzky-Golay (same `mode="interp"`
  implementation family as trackcv's camera-shift smoother, polyorder 3
  for the position curve), percentile / min-max normalisation, optional
  dynamic-range lift, scipy-compatible peak/valley detection with
  distance + prominence, adaptive keyframe densification, minimum action
  interval, RDP via the existing `motionx.Simplify`, speed limiting, and
  tf/tj position clamping. GEMESSEN against committed Python goldens
  (`posttrack/testdata/positions_goldens.json`, regenerated from the
  live `generate_funscript.py` functions): every fixture matches the
  Python action list exactly, and the dense normalised curve stays within
  0.05 position units abs — well under half a funscript step.

  Wiring: `Options.NativePipeline` (GUI: "Go-Pipeline (experimentell)"
  under Erweiterte Einstellungen). When set and eligible (CSRT, one ROI,
  no Tf/Tj / per-scene / AI opinion / audio / auto-retry / OpenCL),
  `GenerateWithProgress` calls `GenerateNativeCSRT` (`trackcv` +
  `posttrack` + JSON write) and never starts Python. Otherwise it logs
  and falls back to the existing Python path. Build tags keep Windows
  cross-compiles CGO-free: `native_track_opencv.go` (`cgo && !windows`)
  imports trackcv; `native_track_stub.go` returns
  `NativeTrackingAvailable() == false` on Windows / non-cgo builds so
  `cmd/gui-wails` does not pull OpenCV into the Windows binary.

  Also fixed while doing this: `process_one`'s inner `build()` never
  passed `peak_prominence` / `dynamic_range_ms` / `min_action_interval_ms`
  into `positions_to_funscript`, so `--profile weich` (and any explicit
  CLI values for those flags) were silently ignored on the Python path.
  Guarded by `process_one_kwargs_test.py` (AST check on the call site).

  Still Python: Quality Doctor, learned quality model, AI opinion, audio
  check, flow/grid_lk/region_fusion backends, two-point Tf/Tj, per-scene
  ROI, tracking cache, auto-retry. Native is opt-in and unmarked as
  default until real-clip Quality Doctor parity exists in Go.

- **Tf/Tj suction double-floor + Go Script Doctor (September 16, 2026)** —
  operator research notes (timing/phase, 4-zone Tf/Tj, accelerators,
  63 BPM) triaged in `docs/FINDINGS_TIMING_TF.md`. Only the concrete,
  code-verified suction bug was fixed now: `SyncSuctionPosition` no
  longer stacks `liftFloor(MinSuction=0.20)` on top of the 20–90 script
  clamp (0.20→0.36 / 0.90→0.92). Script Doctor (`generator.ScriptQuality`)
  now runs in pure Go via `funscript.EvaluateScriptQuality` — no Python
  for “Skript prüfen”. Larger ideas (4-zone relative graph, PTS timeline,
  full WinML inference stack, 63 BPM attractor) stay deferred until
  golden-clip / hardware evidence supports them. Practical training-side
  device switch shipped: `train_yolo_model --device auto|cuda|directml|mps|cpu`
  + GUI dropdown (`ListRoiTrainingDevices`).

- **Research pack → lean ENGINE.md (September 17, 2026)** — dated
  multi-file research dumps removed; direction in `docs/ENGINE.md`.
  Measurable P0 follow-up shipped: `BestLagCorrelation` / `DiagnosePhase`
  (+ CLI). The 8-point PTS→device chain stays deferred until a concrete
  clip set needs it.

- **First real Tf/Tj golden-clip measurement (September 21, 2026)** — the
  user supplied the first actual comparison data `docs/FINDINGS_TIMING_TF.md`
  had been waiting on ("Populate goldens / FunGen refs — you"): SamNPlayer's
  `tj`-profile output for `clip_voll` (Python path, classical CV tracking,
  280s) against two FunGen 2.6.3 references for the same clip — one with
  its own YOLO detector, one with YOLO disabled (classical-vs-classical,
  the fairer comparison per the same reasoning as the September 16
  `region_fusion` measurement).

  **Whole-clip correlation (existing `SamNPlayer phase` / `fungen_compare.py`
  methodology, ±3000ms lag search): r≈0.06 against both references** —
  looks like noise at first glance, matching the user's own suspicion that
  something is fundamentally wrong (possibly their own ROI-marking
  understanding for Tf/Tj).

  **That reading turned out to be wrong once measured at finer resolution.**
  Splitting the same clip into 30s windows and running the SAME best-lag
  correlation independently per window (ad hoc script, not yet a CLI
  option — see below) gives a completely different picture:

  | metric | whole-clip | per-30s-segment (mean of 8) |
  |---|---|---|
  | r vs. FunGen "ohne YOLO" | 0.060 | **0.318** |
  | r vs. FunGen "mit YOLO" | 0.065 | **0.321** |

  Per-segment lag ranged from **-2600ms to +2800ms** and drifted
  continuously across the clip instead of sitting near a constant offset -
  exactly what a single whole-clip lag search cannot capture (it isn't
  "the lag is large", it's "the lag keeps changing"), and exactly finding
  F-003 from `docs/FINDINGS_TIMING_TF.md`'s source material (FrameIndex/FPS
  imprecision under VFR). Direction (`orientation`) also flips from
  `normal` to `inverted` partway through the clip - around 150s against
  the no-YOLO reference, around 210s against the YOLO reference (close but
  not identical, consistent with the flip being a real event in the
  generated curve or the source video, not a comparison-script artifact).

  **Reading on the user's own question ("liegt es an meiner ROI-Markierung
  für Tf/Tj?"):** the per-segment r≈0.32 is in the same range as this
  project's other measured real-clip backends (`csrt`/`region_fusion`
  against FunGen2: r≈0.25–0.37, see the September 16 entries above) - so
  the ROI-marking approach is producing a genuinely correlated signal, not
  a fundamentally wrong measurement. The dominant problem measured here is
  **timing drift**, not marking semantics. The orientation flip is the one
  finding that COULD be a marking issue (e.g. which region was tracked as
  "tip" vs. "partner" changing meaning if the scene/position changes
  mid-clip) - worth checking against the source video at ~150-210s
  specifically, rather than re-deriving the whole marking approach.

  **Concrete, scoped next step (not done yet):** `docs/FINDINGS_TIMING_TF.md`
  already lists "Phase Analyzer core in Go" as shipped
  (`BestLagCorrelation`/`DiagnosePhase`, CLI `phase`/`compare`) - but that
  tool only ever computes ONE lag/orientation for an entire clip. This
  measurement shows that's the wrong granularity for a clip where the
  true offset drifts: add a windowed mode (`--window-ms`, reporting
  lag/orientation/r per window, e.g. a `phase --window-ms 30000` CLI
  variant of what the ad hoc script above did) so a drifting-vs-constant
  offset is visible directly instead of requiring a one-off script per
  investigation. This is a small, well-scoped addition to the *existing*
  `funscript.BestLagCorrelation`/`DiagnosePhase` machinery, not a new
  subsystem - the "full PTS→device phase chain" (8-point timeline,
  deferred in `docs/FINDINGS_TIMING_TF.md`) is a separate, bigger question
  this does not answer or require.

  **Update (same day):** the user confirmed committing the raw funscripts.
  Now at `generator/testdata/golden_clips/clip_voll_tftj/` (`mit_yolo/` +
  `ohne_yolo/` subfolders, `README.md` with reproduction commands) -
  source `clip_voll.mp4` itself stays local per `docs/GOLDEN_CLIPS.md`
  (license + size), only the small `.funscript`/`.samn` files are
  committed. Durations: 277.1s (no-YOLO ref) / 279.999s (YOLO ref) /
  280.1s (SamNPlayer) - within a few seconds of each other, not the ~54s
  clip/reference mismatch seen in the September 16 MILF-clip comparison,
  so duration alignment is not the confound here.

  The windowed-correlation "ad hoc script" mentioned above is now a real
  tool: `generator/fungen_compare_windowed.py` (+
  `fungen_compare_windowed_test.py`), same style/pattern as
  `fungen_compare.py`. Verified it reproduces the finding against the
  committed dataset: whole-clip r=0.060/0.065, windowed (30s) mean
  r=0.268/0.349, lag drift up to -2600..+2800ms, orientation flip around
  150-210s - matching the numbers above.

  **Update 3 (same day): the Go `phase --window-ms` CLI shipped too**,
  separately on `main` (#143: `funscript.WindowedBestLagCorrelation` +
  `FormatWindowedReport`, CLI `SamNPlayer phase A B --window-ms 30000`).
  The Python tool (`fungen_compare_windowed.py`) stays as the offline
  dataset-runner twin, same relationship `fungen_compare.py` already has
  to `BestLagCorrelation`.

  **Update 2 (same day): `hub` profile measured too, same clip.** The
  user supplied a second SamNPlayer run for `clip_voll` - `hub` profile
  (single ROI, no Tf/Tj) instead of `tj` - specifically to test whether
  Tf/Tj mode itself was the problem. It is not:

  | metric | `hub` (mit_yolo / ohne_yolo) | `tj` (mit_yolo / ohne_yolo) |
  |---|---|---|
  | whole-clip r | 0.074 / 0.059 | 0.065 / 0.060 |
  | windowed (30s) mean r | 0.216 / 0.264 | 0.349 / 0.268 |
  | Quality Doctor score | 0.90 | 0.57 |

  `hub`'s Motion Fidelity is not better than `tj`'s despite a much higher
  Signal Quality score (0.90 vs. 0.57, far fewer warnings) - a direct,
  real-clip confirmation of `docs/SIGNAL_VS_FIDELITY.md` ("Signal Quality
  ≠ Motion Fidelity"): the cleaner-looking `hub` curve is not a better
  match to either FunGen reference. `hub` also flips correlation
  orientation far more often across windows (near every window) than
  `tj`'s one clean flip - consistent with `hub` sitting closer to true
  zero correlation, where orientation choice is close to a coin flip
  rather than reflecting one real mid-clip event. Practical reading: the
  low Motion Fidelity measured on this clip is not specific to Tf/Tj
  marking - whatever is driving it (most likely the same timing-drift
  finding above) affects the single-ROI path too. Both `hub` funscripts
  now sit alongside the `tj` ones in
  `generator/testdata/golden_clips/clip_voll_tftj/{mit_yolo,ohne_yolo}/`.

- **Native Go pipeline vs. real FunGen2, `clip_ausschnitt`: same F-003
  drift signature, second real clip (September 21, 2026, corrected same
  day)** - originally published (and merged, #148) as an "internal
  SamNPlayer consistency check" - that framing was **wrong**. Of the
  user's three original `clip_ausschnitt` uploads, two (71 and 738
  actions) carry `metadata.creator: "SamNPlayer generator/native..."`
  plus a `native_pipeline` telemetry block, but are actually **FunGen
  2.6.3's own tracking** (`mit_yolo`/`ohne_yolo`, same convention as
  `clip_voll`) - confirmed when a follow-up upload correctly labeled
  `creator: "FunGen 2.6.3"` matched the 738-action file 715/716 actions
  exactly. Only the third (145 actions, minimal metadata) is genuinely
  SamNPlayer's native Go output. Likely cause: some SamNPlayer
  import/export step stamps its own `creator`/`native_pipeline` metadata
  onto any loaded/re-saved funscript regardless of true origin - a real
  provenance bug, tracked separately from F-003, not yet root-caused.

  With correct labels, this is a real Motion Fidelity measurement (not
  internal consistency), same methodology as `clip_voll`:

  | metric | vs. FunGen2 mit_yolo | vs. FunGen2 ohne_yolo |
  |---|---|---|
  | whole-clip r (native Go) | 0.273 | 0.440 |
  | windowed (10s) mean r | 0.533 | 0.590 |
  | lag range | +0..+700ms | -900..+0ms |
  | whole-clip r (old Python, same refs) | 0.236 | 0.165 |

  Same signature as `clip_voll`: whole-clip r looks weak, windowed r
  rises substantially, lag isn't constant, orientation flips mid-clip
  (F-003, `docs/FINDINGS_TIMING_TF.md`). Native Go's whole-clip r is
  comparable-to-better than the old Python path's against the same real
  FunGen2 references - not a Go-migration regression, but neither path is
  clean. **Correction from the original (wrong) framing**: since a real
  FunGen2 reference *is* involved after all, this does NOT rule out
  cross-tool ROI/timing-convention differences as (part of) the cause the
  way the original entry claimed - it's back to the same open question as
  `clip_voll`, just now confirmed on a second independent real clip.

  Raw files (relabeled with correct provenance, not the source video, per
  `docs/GOLDEN_CLIPS.md`):
  `generator/testdata/golden_clips/clip_ausschnitt_native/`.

- **SAM Perception v1 bake-off: no Go port earned yet (September 21,
  2026)** - `flow`/`grid_lk`/`region_fusion` measured against both real
  goldens per `docs/SAM_ARCHITECTURE.md`'s stated precondition. Neither
  `grid_lk` nor `region_fusion` beats CSRT on `clip_voll` or
  `clip_ausschnitt` (the latter: both land around a third of CSRT's r).
  `flow` hung past timeout on both clips, including the short one -
  contradicts its own "faster than CSRT" docstring, looks like a real
  bug, not measured either way yet. Full table and caveats in
  `docs/SAM_ARCHITECTURE.md` § "Bake-off result". Per `docs/AGENT_COORD.md`:
  no Go port of flow/grid_lk during cleanup; `flow`'s hang is lane E
  (hygiene), not lane B (measurement).

  **Update (same day, lane E/ChatGPT via #156):** found and fixed a real
  bug - the direct `--backend flow` CLI dispatch in `process_one` ignored
  `--flow-downscale`, always running at full resolution regardless of the
  flag. Confirmed a genuine scaling-control bug, not yet confirmed as the
  root cause of the bake-off's specific hangs. Clarifying for lane E: my
  bake-off commands never passed `--flow-downscale` at all (used the
  backend's own default), both clips are 1280x720, so this exact fix
  doesn't change what my invocation would compute either way - the hang
  reproduces even at `flow`'s own default (no-downscale) settings. Root
  cause of that specific hang is still open.

- **F-003's proposed mechanism (VFR/frame-index timing drift) is refuted
  (September 21, 2026)** - direct `ffprobe` check of `clip_ausschnitt.mp4`
  against `generator/trackcv`'s `frame_index × 1000/fps` timestamps: the
  discrepancy is a **constant 41.02ms at every sampled frame, start to
  end** - genuine CFR video, no drift. A constant offset cannot produce
  the observed swinging per-window lag (up to ±900ms, orientation flips)
  - the math rules out the mechanism regardless of any one measurement.
  Also compared raw (pre-`posttrack`) tracking against the FunGen2
  references: r=0.097/0.344, *lower* than the already-committed
  post-processed numbers (0.273/0.440) - `posttrack` isn't corrupting
  otherwise-good timing either. The drift *signature* stays real
  (measured twice, `clip_voll` and `clip_ausschnitt`) and still
  practically means "don't trust whole-clip r alone" - only the *why* was
  wrong. Leading new hypothesis: periodicity aliasing in the lag search
  over near-periodic stroke motion (rough check: observed lag values
  cluster within ±0.2-0.5 cycles of integer multiples of the clip's
  dominant ~280ms period) - suggestive, not proven; needs a synthetic
  clip with known ground-truth lag to test cleanly. Full writeup and the
  exact PTS numbers: `docs/FINDINGS_TIMING_TF.md` § F-003.

- **Periodicity-aliasing hypothesis confirmed with a synthetic
  ground-truth test (September 21, 2026)** - added
  `funscript.TestPeriodicityAliasingCharacterization`
  (`funscript/phase_test.go`, runs in CI, no external fixtures needed):
  two synthetic 60s signals, identical except one is a 280ms-period sine
  wave and the other alternates low/high with a non-repeating random
  half-cycle duration, both given the *same known constant* injected
  300ms lag. Result: the periodic pair's whole-clip lag search reports
  `r=1.0000` at `lag_ms=-1140` (exactly 3 stroke periods off the true
  `-300`), and its 10s-windowed search *swings* `-1420ms..+1240ms` across
  windows with one window's orientation flipping to `inverted` - despite
  the true offset being the same constant 300ms everywhere. The
  non-periodic pair recovers the exact true `-300` lag, `r=1.0000`,
  `orientation=normal`, in every single window - no swing, no flip. This
  proves periodicity aliasing alone (no real drift needed) can produce
  exactly the swinging-lag/orientation-flip pathology observed on the
  real clips. `BestLagCorrelation`/`WindowedBestLagCorrelation` were not
  changed - this is a characterization test, not a fix; any mitigation is
  a separate, not-yet-decided design step. Full writeup:
  `docs/FINDINGS_TIMING_TF.md` § F-003.

- **AliasingRisk flag shipped (#163) but doesn't fire on real goldens yet
  (September 21, 2026)** - Cursor implemented the board-agreed tier-1
  mitigation (`LagCorrelation.AliasingRisk`/`DominantPeriodMs`/
  `AlternateLagsMs`, reported lag/r unchanged, surfaced in `phase` CLI and
  `CompareDataset`). Ran it against all 4 committed real golden pairs
  (`clip_voll_tftj`, `clip_ausschnitt_native`, both yolo variants) -
  `aliasing_risk=false` everywhere, whole-clip and windowed, despite the
  swinging-lag/orientation-flip pattern still being visibly present in
  those same runs. Root cause: the autocorrelation-based period detector
  estimates ~100-140ms on both clips' references, under the code's 150ms
  floor, so it bails before comparing candidates - and 100-140ms looks
  like half the ~280ms period estimated earlier, consistent with the
  known half-period/orientation-flip ambiguity for symmetric stroke
  shapes. Not a code change - a measurement showing the board's "wait
  until it fires on real goldens" condition for tier 2 isn't met yet, for
  a detector-tuning reason rather than an absence of aliasing. Full
  writeup: `docs/FINDINGS_TIMING_TF.md` § F-003.

- **Correction: the "half-period" explanation above was wrong; fixed the
  real bug in `dominantPeriodMs` (September 21, 2026)** - dumping the raw
  autocorrelation curve for both real clips' references showed no local
  peak anywhere near 280ms at all; it just declines monotonically from
  the smallest tested lag, because a smooth continuous motion curve
  correlates strongly at short range regardless of periodicity. The old
  `dominantPeriodMs` took the global max over the whole search range,
  which for a declining curve is trivially the smallest lag tested - and
  a second bug (`minLag := MinDominantPeriodMs / stepMs`, integer
  division rounding down) let that floor itself sit below the documented
  150ms for most resample steps. Both together explain the observed
  100-140ms precisely: it was just the search's own too-low lower bound,
  not a period measurement. Fixed: the floor now rounds up, and the
  function only accepts a genuine local peak reached after the curve's
  initial decline - a monotonically-declining curve now correctly
  returns 0 ("no confident period") rather than a spurious floor value.
  `BestLagCorrelation`/`WindowedBestLagCorrelation`'s reported `LagMs`/`R`
  are unchanged; only the `AliasingRisk` diagnostic is affected. Locked
  in by `TestDominantPeriodMsRequiresGenuineLocalPeak`
  (`funscript/phase_test.go`). Re-measured against the real goldens: the
  flag now fires on 1 of 4 real pairs (`clip_ausschnitt.funscript`
  mit_yolo, whole-clip: `aliasing_risk=true lag_ms=-800
  dominant_period_ms=1500 alternate_lags_ms=[700 800]`) - a real,
  considered result this time, not an artifact. Full writeup:
  `docs/FINDINGS_TIMING_TF.md` § F-003.

## Stroke preview — extrema + Abtastung (owner idea, 22 Sep 2026)

**Idea (owner):** estimate **up-endpoints** and **logical down-points** from
motion via sparse sampling to get **faster** timing; combine later with
other modes for accuracy; **audio only when the preview/track looks bad**;
use the same probe to help flag **hard cuts** and **camera motion**.

**Product rule (locked):** preview is a **Vorstufe**, not a tracker.
CSRT hub / marked tip / Tf-Tj remain the position path. Audio stays
post-hoc / gate-only (`docs/AUDIO_WORKFLOW.md`) — never invent 0–100 from
loudness. Fits G1 classical heuristics (`docs/GENERATE_HEURISTICS.md`),
not a new GUI backend.

### Stages

| Stage | What ships | Gate |
|-------|------------|------|
| **A — sparse probe** | `generator/strokepreview` + CLI `stroke-preview`: peaks/valleys times, stroke Hz hint, cut events, pan share, `quality` ∈ {ok,weak,unstable}, `suggest_audio_check` | Synthetic bounce CI; `clip_ausschnitt` wall-time ≪ CSRT; extrema overlap vs hub peaks (directional) |
| **B — steer** | Use flags to bias re-anchor / peak distance / “run audio check” after track | Golden Motion Fidelity not worse; audio still never writes actions |
| **C — extrema as main path** | **Rejected until A/B prove value** | Would need hub-beating numbers |

Related earlier work: `region_fusion` / `region_fusion_auto` were the
**“Gitter + Abtastung”** position experiment (measured, competitive but not
default). Stroke preview reuses the *sampling* idea for **timing + flags**,
not for replacing CSRT.

### CLI

```text
go run ./cmd/cli stroke-preview /path/to/clip.mp4
go run ./cmd/cli stroke-preview clip.mp4 --json --max-seconds 50
```

### First measure target

Known Claude golden: `tmp/clips/clip_ausschnitt_b76a.mp4` vs FunGen
`ohne_yolo` / committed hub scripts under
`generator/testdata/golden_clips/clip_ausschnitt_native/`.

**First run (22 Sep, Stage A on this clip):**

| Metric | Value |
|--------|------:|
| Wall clock | **~1.1 s** for ~50 s @1280×720 (analysis 12 fps, keep 1/2 → ~167 ms steps) |
| quality | `ok` (suggest_audio=false) |
| stroke_hz | ~1.50 |
| extrema | 62 up / 62 down |
| cuts | 0 |
| pan_share | 0.61 (high — flag noisy; stroke still clear) |
| Peak overlap ±300 ms vs hub | **14/18 = 0.78** |
| Peak overlap ±300 ms vs FunGen ohne_yolo | **16/25 = 0.64** |

Directional only: preview is not Motion Fidelity.

**Wire-in (Stage B start, same PR train):** Generate pre-pass runs
`strokepreview.RunQuick` → progress `STROKE_PREVIEW …` → optional audio
gate + peak-distance bias → stamp `metadata.stroke_preview`. Failures
are non-fatal.

### Flow “suddenly long” — traced (22 Sep)

Not a hang. Farneback cost on `clip_ausschnitt` (1280×720, ~1200 frames):

| downscale | wall | ms/frame | note |
|----------:|-----:|---------:|------|
| **0.5** | **~56 s** | ~36 Farneback + ~10 other | measured `FLOW_BACKEND_TIMING=1` |
| 1.0 (full) | ~3–4 min est. | ~4× area | matches owner bake-off timeouts |

Always-on stderr: `FLOW_SUMMARY wall=… ms_per_frame=… downscale=…`.
Product default for unset `--flow-downscale` / GUI 0 → **0.5**.

## Bugfix board (22 Sep) — split tasks

| ID | Task | Owner | Status |
|----|------|-------|--------|
| BF-1 | #176 bugfix PR (SaveContact Sync, #170 cv2, Zone2 copy, arousal) | Cursor | CI green / ready |
| BF-2 | #177 stroke preview Stage A + Generate wire + Flow default 0.5 | Cursor | this branch |
| BF-3 | Stage B finish: cut-rate → PerSceneROI (this run); pan → camera compensation | Cursor | **DONE** #239 |
| BF-4 | #119 Windows AI-train smoke on current build (keep open) | Owner | blocked on hardware |
| BF-5 | Close superseded #170 after #176 merges | Owner/Cursor | pending |
| BF-6 | Flow GUI: show downscale row; Log surfaces FLOW_SUMMARY | Cursor | in BF-2 |
| BF-7 | F-003 tier-2 agreement | Owner | board (#174) |
| BF-8 | Main CI flake `TestArousal…` if still fails without #176 | Cursor | covered by #176 |

## Product requirements

General generator quality and the result on the Sam Neo 2 are the priorities.
Profiles are named `tf`/`tj`; their recipe uses `suction_position` with
vibration off. Calibrate further parameters only after measurement.
Keep classical analysis as the foundation for later AI integration.
