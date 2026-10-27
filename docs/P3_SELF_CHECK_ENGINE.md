# P3: the self-checking engine with a fast lane

**Owner (26 Oct 2026):** the background-subtraction method of the uploaded
"MotionScript Generator" document worked well in practice, above all its
discipline of throwing away samples that do not fit. Think it through, take
over what is good into our automatic path, add the mathematics and the new
values it needs, and build an engine that checks itself, with algorithms that
work together and a fast lane.

This file is the design. It is grounded in a measurement (below) and in the
rules of [`LESSONS.md`](LESSONS.md); every number here comes from synthetic
test patterns (no real media), so each default still passes the P3.8 gate on
the Owner's golden clips before it ships (D-135).

## 1. What the document's method is

Background subtraction (MOG2) → threshold → contours → the largest blob is the
moving object → its centre y is the sample. Then eight post stages:

| # | Stage | Value in the document |
|---|---|---|
| 1 | Gaps | ≤ 500 ms interpolated, longer gaps split the curve into segments |
| 2 | Outliers | median / MAD over ±3 neighbours, factor 5, replaced by interpolation |
| 3 | Smoothing | zero-phase low-pass, 120 ms |
| 4 | Detrend + normalise | 4 s rolling baseline, 2nd / 98th percentile → 0 / 100 |
| 5 | Turning points | prominence ≥ 10, distance ≥ 60 ms |
| 6 | Speed limit | optional |
| 7 | Simplification | optional |
| 8 | Output | `{t, p}`, integer, increasing |

## 2. Measured: where it is good, where it breaks

Ten synthetic scenes, 640×360, 30 fps, 30 s, textured bodies on a textured
background; the truth is the world motion of the stroke part (camera and zoom
excluded). Score: correlation with the truth at the best lag in ±500 ms, and
the median of 5 s windowed correlations (GD-05/GD-06). "SamNPlayer" is the 0.7
automatic chain (Find tip → CSRT with camera compensation → post), which Next
reproduces bit-identically (P3.1/P3.2 parity). |r| is shown; the document's
convention flips the sign.

| Scene | Document | Document + stabilisation | SamNPlayer |
|---|---|---|---|
| s1 one mover, still camera | 0.92 | 0.89 | 0.99 |
| s2 two bodies | 0.30 | 0.64 | 0.98 |
| s3 two bodies, camera moves | 0.02 | 0.51 | 0.98 |
| s4 two bodies, zoom | 0.11 | 0.33 | 0.87 |
| s5 bigger irregular mover beside the stroke | 0.07 | 0.06 | 0.93 |
| s6 lighting flicker | 0.52 | 0.50 | 0.99 |
| s7 whole body bounces with the stroke | 0.89 | 0.75 | 0.99 |
| s8 tempo changes and pauses | 0.22 | 0.34 | 0.91 |

Runtime per 30 s clip: document about 4–5 s, SamNPlayer about 60 s.

**Reading.** The document's method is right in its ideal case (one mover, still
camera, or the whole body moving with the stroke) and fast. It fails whenever
something else moves more than the stroke part, because "largest blob" picks
the object, not the stroke: a second body, a camera move, a zoom, flicker. Image
stabilisation helps the camera case (0.02 → 0.51) but cannot fix "largest blob
wins" (s5 stays 0.06). Using the blob only to propose the tracking region
(hybrid) was no better than Find tip and broke on camera motion (s3 0.10).
So: **the blob is not a writer.** What is worth taking is its discipline and its
speed: per-sample validity, outlier rejection, gap semantics, and a cheap
foreground map as an independent witness.

This matches what 0.7 learned: one box writes the stroke (GD-01), and a
frame-difference tip finder was rejected (REJ-28).

## 3. Measured: can the engine find its own mistakes?

A second experiment asked whether cheap, independent checks find the windows
where the writer is wrong, without flagging the good ones, and whether a
selective retry repairs only those.

- Writer: Find tip → CSRT (the one writer, GD-01).
- Two hard scenes were added: **s9** an occluder crosses the stroke at 8–10 s
  and 19–20.5 s; **s10** a scene cut at 15 s, after which the stroke happens
  elsewhere in a mirrored picture.
- A 2 s window is *bad* when the writer's correlation with the truth there is
  below 0.5; *flagged* when a checker fails (§5).

Checkers c1–c4 of §5, 13 windows per scene (3–29 s):

| Scene | Bad windows | Caught | Missed | False alarms | Median windowed r before → after repair | Bad after |
|---|---|---|---|---|---|---|
| s1–s7 | 0 | – | 0 | 0 | 0.98–1.00, unchanged | 0 |
| s8 tempo, pauses | 1 | 1 | 0 | 0 | 1.00 → 1.00 | 1 |
| s9 occlusion | 10 | 10 | 0 | 0 | 0.00 → 1.00 | 2 |
| s10 scene cut | 7 | 7 | 0 | 0 | 0.16 → 1.00 | 0 |

- **Detection works:** all 18 bad windows were flagged, none of the 112 good
  ones (s1–s8 including camera motion, zoom, flicker and a bigger mover).
- **Repair works when it is verified:** after the cut and after the occlusion,
  a fresh Find tip + CSRT seed at the failed window, kept only where it passes
  the same checks, brought the median windowed r from 0.16 and 0.00 to 1.00.
  A first version that re-found once at the start of the failed run and kept
  the whole retry unchecked failed on s9 (0.00 → 0.13): it seeded inside the
  occlusion. Lesson: re-acquire **per window** and **verify** each one.
- **The window during the occlusion** (s9, 9 s) stayed a gap: correct, nothing
  there can be tracked.
- **One false accept:** the window of the second, shorter occlusion (s9, 19 s)
  was repaired and passed c1–c4 with r 0.32. c1–c4 are not enough to verify a
  repair; the appearance check against the original seed (c7) is required
  before a repaired window is kept.
- **A pause is not a failure:** the s8 window at 11 s is a stroke pause; the
  writer is right (flat), but the checkers flag it because there is nothing to
  correlate. The verifier needs a fourth state, **still**, set when the fast
  lane sees no motion at the stroke's tempo in that window (c5, c8).

### 3.1 The C++ verifier on the same scenes (SC-1, SC-3)

`engine/include/samn/engine/self_check.h` (`GateSamples`, `VerifyWindows`)
was run on the per-frame signals of the same first runs (writer track, flow in
the box, foreground in the box, tracker ok, fast-lane scene motion; the cut of
s10 given as scene_cut would report it):

| | Bad windows | Failed | Missed | False alarms | Still |
|---|---|---|---|---|---|
| with the fast-lane scene motion | 17 | 17 | 0 | 0 | 1 (the s8 pause) |
| without it | 17 | 17 | 0 | 1 (the s8 pause) | 0 |

Two things the first version got wrong and the tests now pin:
- **Windows across a scene cut** passed by accident (the cut frames look like
  big motion to flow and background model). Windows are now tiled per shot
  and the first 5 frames of a shot get no verdict.
- **The range reference** was the median window range; with more than half
  of the windows broken (s10 after the cut) the median was the broken range.
  It is now the 75th percentile.

Without an independent signal a pause cannot be told from a box on still
background, so the verifier fails it instead of guessing: "still" needs the
fast lane's quiet scene or the seed's appearance (c7).

### 3.2 First real clip (Owner clip `owner-av1-256x144`, private test media)

256×144, 570 s from the Owner's box at 305 s; reference = the 0.7 script and
0.7's own tip path (`trajectory.tip`). There is no ground truth, so "right"
means "near 0.7's box".

- The writer here was **plain CSRT** (no camera compensation, no scene
  re-anchoring, unlike the full Next chain). It followed 0.7's box for about
  10 s, then drifted. From 20 s on it was **pinned at the picture border**
  for 59 % of all frames; median distance to 0.7's box 112 px in a 256 px
  frame.
- **c1–c8 missed that:** 102 of the 129 windows they passed had the box at
  the border. CSRT still found motion there, so flow and foreground agreed.
- **c9 "box outside the picture"** (share of the box area outside the frame;
  geometry, not the tracker's opinion) fixed it. 167 of 168 border-pinned
  windows now fail; passes fell from 129 to 28; of the 8 windows where the box
  is within 25 px of 0.7's box, 7 pass.
- So on this clip the verifier says, correctly, that a single CSRT seed is
  wrong for most of the clip. That is the case the repair (SC-5) exists for.
  Next measurement: the full Next chain (camera compensation, cuts) as the
  writer, then SC-5, against 0.7's windowed r (README of the clip: median r
  0.49 over 30 s windows).

### 3.3 The real C++ chain with recorded checks (SC-2)

`TrackBox` with `record_checks` (the Next chain: CSRT, camera compensation,
scene cuts, appearance memory, guards) and `VerifyWindows` on its signals.

**Synthetic scenes** (Find tip box from 0.7, 2 s windows): 13 of 13 bad
windows fail, 0 false alarms; the four pause windows of s8 are "still"; the
zoom scene gives 7 "suspect" windows (c7 alone, see below). The chain itself
is better than the plain CSRT of §3: the scene cut of s10 leaves no bad
window. Three rules came out of this run and are now pinned by tests:

- **Scene motion from flow, not MOG2.** MOG2 learns a regular stroke into its
  background model within a few cycles, so a steady scene read as "nothing
  moves" and a box on still background passed as a pause. The scene motion
  is now the share of pixels whose flow differs from the frame's median by
  more than 1 px, and a pause is a drop against the clip's usual level.
- **c7 is a trigger, not an independent checker.** The seed template does
  not follow a zoom: on s4 the box was right (c1 0.99) while c7 fell to 0.26.
- **Wrong axis.** On s9 one jump made the axis choice pick x while the stroke
  stayed vertical; the flat x track with a matching look read as a pause.
  The flow on the other axis (`box_flow_cross`) now turns such a window into
  a failure ("motion on the other axis"). The axis choice itself uses the raw
  range (`Ptp`) and should use a robust spread or the principal axis (§5b).

**Owner clip** (full chain, 570 s): the box stays near 0.7's (median 15 px,
64 % of frames within 25 px; the plain CSRT of §3.2 was 112 px off). But the
scene-cut test fires 1992 times (as 0.7's did, about one frame in eight at
256×144); windows never span a cut, so 37 % of the frames get no verdict, and
on the rest "fail" and "pass" windows are about equally far from 0.7's box
(33 and 30 px). "Still" windows sit near 0.7's box (9 px). So on this clip the
verifier is not yet useful, and the first thing to fix is the cut detector
(fast lane, SC-4), measured on this clip, before any threshold is tuned.

### 3.4 Adaptive cut test on the Owner clip, and what the clip cannot tell

`TrackOptions::adaptive_cuts` (`AdaptiveSceneCut`: the frame difference must
also be 3 x the median of the last 2 s) brings the Owner clip from 1992 cuts
to **2**, and 99 % of the frames get a verdict. But the **tracking got worse**:
median distance to 0.7's box 15 px → 144 px. The false cuts were working as
a frequent re-anchor: every "cut" ran the appearance memory's reacquire,
which pulled the box back to the start look. Without them CSRT drifts. So the
option stays **off** (0.7 parity), and the re-anchoring role has to move to
the verified repair (SC-5) before the cut test can change.

On this clip the verdicts do **not** separate good from bad windows yet:
against the 0.7 script (2 s windows, |r|) "fail" windows agree as well as or
better than "pass" windows (old cuts: 0.61 vs 0.42; adaptive: 0.42 vs 0.37).
The reference itself is a post-processed 0.7 script, and windows of 2 s on a
script with fill points are noisy, so this clip cannot calibrate thresholds.
The thresholds stay those measured on synthetic scenes and are not tuned on
this clip; calibration needs the golden clips with a checked reference
(D-135), as planned for SC-6.

### 3.5 Verified repair (SC-5) on the Owner clip

First run: the full chain (0.7's cut rule), 67 failed 2 s windows. Repair as
built (Find tip from the failed window, up to 3 proposals, each retry tracked
with the original look as the memory's first template; a retry window is
kept only if it passes outright and its look against the original is ≥ 0.75):

- **0 of 67 repaired, 67 gaps** (24 % of the clip without a writer). Against
  the 0.7 script (5 s windows) the median r went 0.20 → 0.18; of the 16
  windows the gaps changed, 11 agree better and 5 worse.
- **Why:** the retries pass the motion checks (c1 0.83-0.90 on the probed
  windows), but their look against the start crop at 305 s is only
  0.55-0.59. At 256×144 the crop is about 24×22 px after downscaling, and the
  pose changes over ten minutes; on the synthetic clip a background patch
  matched a foreign seed at 0.59 too. **The appearance check cannot tell the
  right target from a wrong one on this clip**, so fail-closed means "no
  repair".
- Cost: the run took about 40 min for 570 s (Find tip on 5 s plus up to three
  tracking runs per failed stretch). Needs caching of the flow (§5c) and
  shorter retries before it is usable.

What would verify identity better than the start crop (to be measured, not
assumed): continuity with the last verified window (the retry starts within
one box of where the good track was and has its stroke frequency), the
rhythm-grid cell of GD-15, or a detector class (YOLO-family, §5a). Until one of
these is measured, the repair stays strict: a gap is better than a wrong box
(GD-19).

## 4. The engine: three lanes

```
            ┌──────────────── fast lane (draft, steers only) ────────────────┐
 video ───► │ ¼-size frames, every 2nd: cuts · camera (RANSAC inliers) ·    │
            │ foreground map · rhythm grid / tempo · stroke preview          │
            └───────────────┬──────────────────────────────┬────────────────┘
                            │ shots, tempo, regions, refs  │
                            ▼                              ▼
            ┌──── writer lane ────┐            ┌──── checker lane ────┐
            │ Find tip → CSRT box │──── y ────►│ c1…c8 per frame       │
            │ camera compensation │            │ (independent sources) │
            └─────────┬───────────┘            └──────────┬────────────┘
                      │                                   ▼
                      │                     verifier: 2 s windows → pass / suspect / fail / still
                      │                                   │
                      ▼                                   ▼
               sample validity  ◄──── repair: re-acquire per failed window,
               Hampel, gaps            verify like the first run, else gap
                      │
                      ▼
               post chain (P3.2) → actions + quality report
```

### 4.1 Fast lane

A pre-pass at about 320 px width on every second frame. It is the document's
speed put to work as a **draft and a witness**, never as the writer:

- **Shots:** scene cuts (already in `scene_cut`), so every later step works per
  shot.
- **Camera:** feature tracking + RANSAC partial affine; the **inlier fraction**
  is the trust value of the camera estimate (GD-14). The full-size camera
  estimate stays in the writer lane (half size cost r 0.94 → 0.71, REJ-24);
  the fast lane only says where the camera is unreliable.
- **Foreground map:** MOG2 on the stabilised small frames. Per shot it gives the
  regions that move and a reference foreground level for the checker c2.
- **Rhythm:** the rhythm grid of GD-15 (flow at 320 px, camera median removed,
  16×9 cells, 8 s windows every 2 s), giving the tempo f0 per window. It sets
  the detrend window (GD-12), the minimum peak distance and the periodicity
  checker c5.
- **Stroke preview:** extrema count and stroke rate (GD-20).

Output: a `FastLaneReport` (shots, tempo per window, camera trust per frame,
foreground references, candidate regions). Rule of GD-20: it **steers only**; it
never writes actions. It also gives the user a first picture of the clip within
seconds, labelled as a preview.

### 4.2 Writer lane

Unchanged: the tip box from Find tip (or the user's box), tracked by CSRT with
camera compensation, appearance memory, jump guard and coast (P3.1). One box
writes the stroke (GD-01). The fast lane may set the start region per shot
(through the search anchor, GD-16), never the positions.

### 4.3 Checker lane

Each checker is computed per frame, aggregated per 2 s window (GD-14: dense,
detrended, windowed, as fractions). Independent sources come first; the
writer's own values are triggers only (REJ-32, GD-11: CSRT is wrong by place,
not by loss).

### 4.4 Verifier, repair and gaps

- Each window gets **pass**, **suspect**, **fail** or **still** with the reasons (which
  checker, which value). Checkers are never averaged into one number that
  could hide a single clear failure (REJ-05 in spirit: agreement shows
  disagreement, not who is right).
- **Repair** runs only on failed windows: Find tip again on that window of the
  shot (fast-lane regions as anchors), a fresh CSRT seed, tracked to the end of
  the failed run. A repaired window is a **candidate**: it is kept only if it
  passes the same checkers (§5) *and* its appearance matches (c7, required: c1–c4 alone let one wrong repair through in §3) the original seed
  or a verified re-acquisition (GD-10). Otherwise the window stays a **gap**
  (GD-19: lost is not wrong, a gap is honest).
- **Gaps:** ≤ 500 ms interpolated (the document's rule); longer gaps split the
  curve. The rhythm bridge of GD-19 may fill a long gap only as an opt-in.

## 5. The mathematics

Notation: window W (2 s, 60 frames at 30 fps), writer position y_t (image y of
the box centre, camera-compensated), v_t = y_t − y_{t−1}, corr = Pearson
correlation over the frames of W where both sides are valid (at least 10
frames and a non-zero spread, else the checker reports "unknown").

| Id | Value | Definition | Fail when | Kind |
|---|---|---|---|---|
| c1 | flow agreement | corr(v_t, f_t), f_t = median vertical Farneback flow inside the box − median outside (camera), at ½ size | < 0.4 | independent |
| c2 | foreground presence | mean MOG2 foreground fraction inside the box ÷ the shot's reference (median of its first 10 s) | < 0.3 | independent |
| c3 | Hampel rate | share of samples with \|y_t − m\| > 5 · (1.4826 · MAD + ε), m and MAD over the ±3 neighbours, ε = 0.5 px | > 0.1 | own (trigger) |
| c4 | tracker ok-rate | share of frames with a CSRT box | < 0.7 | own (trigger) |
| c5 | periodicity | E(f0)+E(2 f0) over the total power of the detrended box signal, f0 from the fast lane (GD-15 score) | < 0.2 with a tempo present | independent of the box's own tracking |
| c6 | camera trust | RANSAC inlier fraction of the camera estimate (GD-14) | < 0.4 | context: c1 is "unknown" when this fails |
| c7 | appearance | match of the box against the **original** seed template (GD-11 `matchesOriginal`, window sized from the original) | below the P3.1 gate | independent |
| c9 | box outside the picture | mean share of the box area outside the frame (`BoxOutsideShare`) | > 0.3 | independent (geometry) |
| c8 | range collapse | (p98 − p2) of y in W ÷ the median of that over the shot | < 0.25 | own (trigger) |

Window state: **still** if the fast lane sees no motion at the stroke's tempo in the window and c8 shows a collapsed range (a pause, kept as written); else **fail** if any independent checker fails, or two triggers fail
together; **suspect** if one trigger fails alone; **pass** otherwise. A checker
that cannot be computed is "unknown" and neither passes nor fails a window.

Sample rules from the document, applied to the raw track before the P3.2 chain:

- **Validity:** a sample is invalid when the tracker has no box, when it falls
  in a failed window that repair could not fix, or when c3 marks it as an
  outlier.
- **Hampel:** outliers (c3) are replaced by linear interpolation from their
  valid neighbours (k = 3, factor 5, MAD · 1.4826).
- **Gaps:** invalid runs ≤ 500 ms are interpolated; longer runs split the
  curve; each segment is normalised on its own (`PostOptions::scene_ranges`
  already does that per shot).

c1–c4 were measured in §3, c8 and c9 on the Owner clip in §3.2; c5–c7 are not measured yet. Thresholds above are first values. Each is pinned from both
sides with a test (GD-15 rule) and calibrated on the golden clips at P3.8.

## 5a. AI building blocks in the engine (Owner, 26 Oct)

The Owner asked for YOLO, MediaPipe, OpenCV deep-learning models, Whisper and
CLIP in the engine. Each one gets a role inside the three lanes. None of them
writes the stroke (GD-01, D-064, D-139). Each one is chosen by measurement
(`samn_compare`, D-143), not by its name. Inference runs in C++ through ONNX
Runtime (already in the build, D-155) or OpenCV dnn. Python is only for
training (D-108).

| Block | Role in the engine | Lane | Licence (code / weights) | Notes |
|---|---|---|---|---|
| **YOLO** (v8 / 11, ONNX) | Body-part boxes: proposals for Find tip and for verified re-acquisition (SC-5, P3.7); an independent checker "is the box on the tracked body part" (new c10) | fast lane (every n-th frame) + repair | Ultralytics: AGPL-3.0 for code **and** weights → if it wins the measurement, the project moves to AGPL-3.0-or-later (D-143) | RF-DETR (Apache-2.0) is our own student (D-155) and the first challenger; NudeNet (teacher) is already in AI Train. |
| **MediaPipe** (hands, pose, face) | Pose / hand keypoints as a second, independent witness: is a hand or a hip at the box; its keypoint motion against the box motion (like c1) | checker lane, fast lane | Apache-2.0 (code and the published models) | The C++ framework needs Bazel; the plan is its models as ONNX/TFLite through ONNX Runtime, not the framework. Licence of each model card checked first (GD-17). |
| **OpenCV + DL models** | Already the base: DIS/Farneback flow (c1), MOG2 (c2), RAFT (BSD) as a stronger flow checker; TrackerNano/Vit as challengers to CSRT, never a silent swap (REJ-12) | all lanes | Apache-2.0; RAFT BSD | P3_GENERATOR "Own engine and C++ toolbox". |
| **Whisper** (whisper.cpp) | Speech-Hold (D-157 phases); speech and sound segments as context for the verifier (a window full of speech and no motion is a likely pause, not a failure) | fast lane (audio beside video, D-142) | MIT (code); weights MIT | Never a position source (REJ-26). Silero VAD (MIT) first, Whisper only where words matter. |
| **CLIP** (OpenCLIP, ONNX) | Scene type per shot ("close-up", "two people", "empty picture", the D-157 phases) for the fast lane: it chooses the profile, the detrend window and whether repair should run; also search by text in the Editor | fast lane | OpenAI CLIP MIT; OpenCLIP MIT (weights: check each checkpoint's dataset licence) | A proposal the user confirms (D-129); a wrong scene type must not override the measured signal. |

The order of work: (1) YOLO-family proposals for SC-5 repair, because the
Owner clip shows that the repair needs a re-anchor (§3.2); (2) MediaPipe
keypoints as c10/c11 checkers; (3) CLIP scene type in the fast lane;
(4) Whisper/VAD with the phase work. Each step has the same gate: no
regression on the goldens, and a win on clips where today's result is wrong
(GD-16).

## 5b. What the MTFG workflow teaches (Owner link, 26 Oct)

Source: a community walkthrough of Motion Tracking Funscript Generator v0.5
(the OFS extension from `michael-mueller-git/Python-Funscript-Editor`; check
its licence before copying any code, ideas are free). Its author tracks by
hand, segment by segment, and fixes the rest manually. Every manual step is
a decision our engine should make itself, and check:

| What the user does by hand in MTFG | What the engine does instead | Where |
|---|---|---|
| Splits the clip into segments by kind (one moving part → y; two moving people → their distance; a sideways motion → x; untrackable → skip) | Fast lane: shots, scene type (CLIP, §5a), tempo; per segment the method (single box, two-box distance GD-08) and the "skip" decision (verifier fail → gap) | SC-4 |
| Rotates the view until the motion lies on an axis | **Principal motion axis:** project the box path onto the first principal component of its displacement in each window (PCA of dx, dy), instead of choosing x or y. Needs no user and also settles the x/y question the Owner clip raised (0.7 followed x there, Next first chose y). New value, measured against x/y on the goldens before it is a default. | SC-2 / P3.2 |
| "Supervised tracking": a stay area; stop or ignore data when the feature leaves it | The verifier's c9 (box outside the picture) generalised to a stay area per shot (from the fast lane's moving region or the user's mark); samples outside are invalid (sample gate) | SC-1 / SC-3 |
| Stops tracking before an occlusion | Verifier fail → gap, then verified re-acquisition | SC-3 / SC-5 |
| Tracks "anything distinct that moves in sync" when the main part is hidden | Find tip and re-acquisition may anchor on any feature whose rhythm matches the stroke (rhythm grid GD-15, c5) | SC-5 |
| Tracks fast and slow sections separately with different point density | Post-processing parameters per tempo segment from the fast lane (peak distance, simplification) | SC-4 + P3.2 |
| Processes every frame when the motion is fast | Never skip frames in the writer lane; only the fast lane subsamples | (kept) |
| Keeps the timing from tracking, fixes the stroke length by hand ("true to the video, but nobody can feel it") | Amplitude stays a profile decision (dynamic range, feel recipes); timing is what the self-check guarantees | P3.2 / feel |
| Post-processing tabs: local min/max, second derivative, distance minimisation, evenly spaced intermediate points | Already covered by peaks + adaptive keyframes + RDP (P3.2); "evenly intermediate" points are a candidate profile option, measured before it is added | P3.2 |

## 5c. Other generators and scripting guides (Owner links, 27 Oct)

**FunAutoScripter** (MIT, Python, `pingasAtumas/FunAutoScripter`): fully
unattended. Farneback flow on 192 px-wide gray frames, averaged per 12 px
cell; the region is the most periodic and most moving box, re-chosen every
3 s (switch only above 0.6 x the current score); the axis is a
magnitude-weighted PCA per window; the stroke is the flow projected on the
axis, integrated, then a zero-phase 0.15-5 Hz band-pass; rolling 10 s
percentile normalisation (3/97); peaks with prominence 12 and 0.12 s spacing;
speed cap 450. Scene cuts from flow-magnitude spikes (> 4 x the 2 s median);
chapters per cut and axis change. No confidence, no rejection.
What we take: the **cut test as a ratio to the recent median** (now
`AdaptiveSceneCut`, §3.3), the **PCA axis** (§5b), **flow cached per clip** so
re-tuning post-processing is instant, and **chapters on axis change**. What
we do not take: the integrated flow as the writer (REJ-03: integration
drifts), the region that "wins" on periodicity alone (its own author: the
region jumps to a distractor when the action pauses; GD-15), no camera
compensation.

**Beat-bar generator** (MIT, `Rantark/beat-bar-video-creator-with-funscript`):
spectral-flux onsets (STFT 2048, hop 10 ms, 60-8000 Hz, 250 ms baseline,
threshold 0.35 - 0.30 x sensitivity, min gap 120 ms); per 10-30 s section the
period from an inter-onset histogram (200-1500 ms) and a regular grid;
one stroke per beat, amplitude from the interval; idle oscillation 30-70
over gaps >= 2 s. Useful for the Editor's beat-bar tools
(`P3_GENERATOR.md` "Editor create tools"): the inter-onset histogram per
section is a cheap tempo estimate for the fast lane too. The grid
regularisation flattens tempo drift and syncopation, so ours keeps the raw
onsets where they disagree with the grid.

**Multi-axis scripting guide** (OFS tutorial): axes L0-L2, R0-R2, V0/V1,
A0-A2, neutral 50 (add-ons 0). Hand rules: roll/pitch as the stroke shifted by
a quarter period (stroke = sin, roll = cos) for circular motion; surge
mirrors pitch, sway mirrors roll; keep extra axes within 10-90; speed caps
per device (Handy 400/500, OSR2/SR6 and Handy 2 600/700). For us: the extra
axes are derived from the measured motion (the box's x/y path and the
principal axis give sway and roll directly), with the quarter-period rule
only as a fallback recipe, and the device caps go into `LimitSpeed` per axis.

**Hand-scripting tutorial series** (Slibowitz, ep. 1): the rules are in the
videos, not the text. From the thread: add points inside a stroke only at a
real stop; when two moving parts are out of sync, follow the dominant one;
speed in units per second, about 400-500 for a Handy. These match our
peak/keyframe rules (P3.2) and `LimitSpeed`.

## 6. Rules this design keeps

| Rule | How |
|---|---|
| GD-01 one writer | Only the CSRT box writes; the fast lane, flow and foreground only check or re-anchor. |
| REJ-05 no fusion | No source is blended into the curve; checkers are not averaged. |
| REJ-28 no frame-difference writer | MOG2 is a checker (c2) and a region proposer, not a writer. |
| REJ-32 / GD-11 own quality is no proxy | c3, c4, c8 are triggers only; a window fails on them only in pairs. |
| GD-10 fail closed | A repair is kept only when verified; else a gap. |
| GD-14 metrics | Dense, detrended, windowed, fractions; camera trust = inlier fraction. |
| GD-19 gaps | Lost is not wrong; long gaps split; rhythm bridge opt-in. |
| GD-20 preview steers | The fast lane never writes actions. |
| D-135 measure first | Every new default passes the P3.8 gate on the goldens. |

## 7. Slices (each one a PR with tests)

| Slice | Content | Done when |
|---|---|---|
| SC-1 ✅ | `GateSamples` (Hampel + gap semantics + segments) in `engine/src/self_check.cpp`; not yet wired in front of the P3.2 chain (off by default when it is) | Unit tests on synthetic tracks (spikes, short and long gaps); P3.2 parity unchanged when off |
| SC-2 ✅ | `TrackOptions::record_checks` → `TrackResult::checks` (flow in the box on both axes, foreground, scene motion, tracker ok, camera inliers, appearance, box outside) | Positions bit-identical on/off; moving, panning and wrong-box clips tested (`tests/engine/track_checks_test.cpp`) |
| SC-3 ✅ (verifier) | `VerifyWindows()` → `WindowVerdict` list (pass / suspect / fail / still + reasons), per shot; into the quality report with SC-2 | The s9 / s10 style patterns are flagged, s1–s8 style patterns are not |
| SC-4 | `FastLane()` → `FastLaneReport` (shots, tempo, camera trust, foreground refs, regions) on a worker thread, progress from frame 1 (GD-21) | Runtime under ¼ of the full run on the same clip; the shot and tempo tests pass |
| SC-5 ✅ (first version) | `RepairTrack` (`engine/include/samn/engine/repair.h`): per failed stretch Find tip from that window, up to 3 proposals tracked with the original look as the memory's first template (`TrackOptions::seed_gray/seed_box`), on the first run's axis; a retry window is kept only when it passes outright and c7 ≥ 0.75; otherwise a gap (NaN); spliced runs are listed for per-segment normalisation | `tests/engine/repair_test.cpp`: a target lost behind a panel is found again and verified; with a foreign original look nothing is spliced in |
| SC-6 | `samn_compare` gate: engine with self-check vs without vs 0.7 on the goldens | Report in `docs/benchmarks/`; default on only after the gate |

The experiment scripts and the synthetic scenes live with the private test
media (not in this repo); this file keeps only the numbers.
