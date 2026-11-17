# Generator detection: findings and work packages (17 Nov 2026)

Status: **analysis only, nothing in this document is implemented yet.** It
records the read-only analysis of main `074386f` (10 Nov), the Owner's
priorities of 13-17 Nov, what Cursor merged in the meantime (#247-#264), and
the ordered work packages. Everything measured here is **synthetic** (one run
per scene, no real clips). Golden clips from the Owner are still missing.

## Owner priorities (13-17 Nov, paraphrased from German)

1. Most videos for scripts are **POV**. VR/stereo is secondary.
2. "Our scripts are currently very bad." Too many settings: like FunGen 2,
   show **4 choices** first, the rest belongs to the **Doctor**.
3. The **tip is the main problem**: it does not follow, or does not work at
   all. Wanted: the tip box is **set automatically like in FunGen 2**; ideally
   one **big tip over everything** (the whole active region).
4. POV: clean **contact**; everything we already have (contact vibration,
   areas, masks) must be laid over it.
5. FunGen 2 is **much faster**.
6. Golden clips: cut the benchmark clip to 3 minutes, and where worthwhile
   first make a **25 MB clip with enough motion**.
7. Quality comes first in everything; CPU/RAM are levers for speed and
   quality, not goals. Order: video detection, audio, CPU/RAM.

## What main already has (Cursor, 13-15 Nov)

#247 live tip overlay; #248 Create finds the tip (multi-window resolve, no box
to paint); #250 Everyday Create surface; #251 POV hint (copy only, no second
ROI); #249 Use audio; #255 AI tags + assist; #257 guest-fast tip; #258 Tip+2nd;
#259 YOLO detector + teacher + student; #262 Everyday at 15 fps; #263 analysis
width 640; #264 Grab-only for skipped frames. **Nobody measured the quality
effect of 15 fps / 640 px / Grab-only.** The bench does not use the app's
defaults.

## Findings (Fable analysis of 074386f, to be re-verified)

### Pipeline map and weak points

| Step | Where | Weakness |
|---|---|---|
| Open | `GeneratorController::openVideo` generator_controller.cpp:677; `FrameReader::Open` vision/src/frame_reader.cpp:32 | reads size/fps/codec only; no layout check |
| Tip find | `FindTip` engine/src/tip_find.cpp:162 (12x8 grid at 320 px, vertical flow only, `CameraMotionY` at 3 px) | scores Y motion only: an X stroke scores 0.13 against 2.24 for the same Y stroke; on stereo every candidate appears twice |
| Tracking | `TrackBox` engine/src/track_box.cpp:169; cut test :334; CSRT :356; jump guard :361; reacquire :366 | long occlusions and jump cuts weakest |
| Scene cuts | `IsSceneCut` engine/src/scene_cut.cpp:32 (histogram < 0.5 or signature > 12); `AdaptiveSceneCut` :36 exists but is off | **false cuts**: shake 132, shake_noisy 50, parallax 37, pan_fg 25, parallax_noisy 18 in 12 s; each skips the tracker update, re-anchors by template and zeroes the camera sum |
| Camera | `EstimateCameraFits` engine/src/camera_motion.cpp:91; gate track_parts.h:62 | a gated frame adds nothing (pan_noisy track r 0.942, stop_noisy 0.768) |
| Script | `Generate` engine/src/generate.cpp:134 | peak error is 10 ms on nearly every scene (saturated, cannot move); amp_ratio 0.80-0.96 |
| Self-check | self_check.cpp:187, repair.cpp:46 | not wired: only `samn_selfcheck` calls `record_checks`, `VerifyWindows`, `RepairTrack` |
| Improve | `ImproveScript` core/include/samn/improve.h:85 | works on actions only; never sees lost flags or verdicts |
| POV | `ClassifyPov` core/src/pov.cpp:106 | (since #251 called from the tip path, seed box as stand-in; `MeasurePovSignals` from video still open) |

Weakest bench rows on 074386f (script r / track r / position p95 px):
light_cut_jump 0.666 / 0.689 / 10.1; occl_4s 0.716 / 0.541 / 47.4; occl_2s
0.772 / 0.835 / 25.7; stop_noisy 0.877 / 0.768 / 57.0; parallax 0.947 / 0.939
/ 30.6; pan_fg 0.952 / 0.846 / 27.4; drop30 0.962 / 0.955 / 18.8.

### Biggest finding: false scene cuts, not the camera model

The 10 Nov doc's explanation for the parallax/foreground weakness ("the fit
flips between layers") is not what the numbers show. Cuts on (as main) against
off:

| scene | cuts | track r | pos p95 px | script r |
|---|---:|---|---|---|
| parallax | 37 -> 0 | 0.939 -> 0.988 | 30.6 -> 9.6 | 0.947 -> 0.983 |
| pan_fg | 25 -> 0 | 0.846 -> 0.998 | 27.4 -> 4.2 | 0.952 -> 0.988 |
| px_near2.5 (near layer 2.5x) | 137 -> 0 | 0.746 -> 0.995 | 37.3 -> 7.8 | 0.966 -> 0.987 |
| shake | 132 -> 0 | 0.992 -> 0.996 | 10.8 -> 7.9 | 0.986 -> 0.988 |
| shake_noisy | 50 -> 0 | 0.993 -> **0.968** | 11.8 -> **16.0** | 0.989 -> 0.985 |

`adaptive_cuts` (already in the engine) gives the same track r on parallax
(0.988) and pan_fg (0.998) but is worse on shake_noisy (0.974, 24 px) and
px_shake. Inference (not verified frame by frame): with 25 % of frames gated,
each false cut zeroes the camera sum and so hides the drift of lost steps.
**Adaptive cuts cannot ship alone; they need a camera sum that does not drift.**

### VR / stereo (secondary)

Detected nowhere (only a file-name strip in script_finder.cpp:193). Synthetic
eyes with 6 px disparity: static/pan/shake fine; **zoom with X stroke on SBS
0.791 / 0.847 (37 / 32 px)**, Y stroke on TB 0.834 / 0.807, against 0.984 mono;
each eye zooms about its own centre so two similarity models compete. Find tip
shows every region twice. Prototype detector: 12 frames, gray 192x96, high-pass,
normalised correlation of the halves over +-10 px shift: SBS/TB 0.81-0.87,
mono <= 0.08 (thresholds synthetic only). A 4096x1536 SBS clip took ~1100
ms/frame; the left eye cropped to 640 px ~103 ms/frame with the same r.
Hook: `DetectLayout` in vision/, `analysis_roi` + analysis scale on the option
structs (note: #263 already added `analysis_width`). Fisheye/180: unvalidated.

### 3-D / parallax camera model

Prototypes (scratch, env switch): RANSAC homography rejected (pan_noisy 0.942
-> 0.828, stop_noisy 0.768 -> 0.597); two-layer fits no gain or worse
(px_near0.4 0.987 -> 0.925 when picking the slower layer). On today's scenes a
depth-aware fit has no measurable gain. The cheapest real gain is the cut fix.
Still weak after it: stop_noisy 0.768, pan_noisy 0.942, px_steady 0.957.

### Settings

The bench sets only `profile = recommended`; the app also sets smooth 11, min
peak distance 150 ms, sliding dynamics 3000 ms, adaptive error 6, gap bridge
500 ms, outlier MAD 5, 100 ms spacing for strokers (generator_controller.cpp
~166-181, defaults generator_controller.h ~414-425), now also 15 fps and 640 px.
**So no bench number describes what a user gets.** Sweep plan: one factor at a
time (smooth {7,9,11,13}, min peak distance {100,150,200}, prominence
{0.1,0.2,0.3}, adaptive error {0,4,6,8}, dynamic range {off,2000,3000,5000},
gap bridge {300,500,800}, MAD {4,5,6}, cuts {fixed, adaptive x window 1-3 s x
factor 2.5-4}, coast {4,8,12}, max_analysis_fps {0,30,15}), then pairs for the
top three. Acceptance: no scene's r drops more than max(0.01, spread); median
r up or peak error down by >= 2 ms; ms/frame and RSS at most +10 %; plus track
r and position p95 no more than 10 % worse (script r hides slow errors). Then
the Owner gate through `samn_compare`.

## Work packages (ranked)

| # | Package | Size | Gain | Risk | Acceptance |
|---|---|---|---|---|---|
| 1 | **Bench truthfulness**: `--preset app`, `--set key=value`, sub-frame peak truth, noise draws; POV / fast-stroke / 4K-down families; golden-clip tool; before/after of 15 fps / 640 px / Grab-only | M | makes 2-7 measurable | low | old rows byte-identical in the default preset |
| 2 | **Automatic tip box (Owner request)** + FindTip on both axes + "one big tip" region | M | tip that exists and follows | medium | tip hit rate, track r, p95 on POV-like scenes; mono rows unchanged |
| 3 | **Cut test that survives camera motion** | M | parallax 0.939 -> 0.988, pan_fg 0.846 -> 0.998, px_near2.5 0.746 -> 0.995 | shake_noisy drops unless 4 lands with it | false cuts <= 2 per scene; true cuts (hard_cut, light_cut_jump) still found |
| 4 | **Camera sum without drift** (leaky/high-passed integration or fit over k-2..k) | M-L | shake_noisy, pan_noisy, stop_noisy | high: five carry variants already failed | rule above over 5 noise draws; 3 not default without 4 |
| 5 | Parameter sweep (needs 1) | M | amp_ratio 0.80-0.96, drop30 20 ms | overfitting: hold out half the scenes | rule above + Owner gate |
| 6 | Wire the self-check behind a switch (record -> verify -> mark gaps -> review card) | L | honest gaps on occl_2s/4s, light_cut_jump | verdicts did not separate good from bad on the Owner clip | loss precision/recall; no r drop elsewhere |
| 7 | POV measurement (`MeasurePovSignals` from video) + contact laid over | L | none measurable yet | high | needs scenes from 1/2 |
| 8 | VR/stereo layout detect + one-eye crop (secondary) | M | zoom 0.79-0.85 -> ~0.98 | false positive on mono: tell the user, override | stereo scenes within 0.01 of left-only |

Parallel worktrees: 1 touches `engine/tools/samn_bench.cpp` and
`tests/engine/synthetic_scenes.h`; 2 touches tip_find / tip_resolve / generator
controller and its own `tests/engine/tip_scenes.h`; 3+4 touch `track_box.cpp`
:334-483, `scene_cut.cpp`, `camera_motion.cpp` (one worktree, in sequence);
5 is controller defaults and `signal_chain.cpp`; 6 touches generate.cpp,
generator_controller.cpp, QML (after 5).

## Not measured

Real clips of any kind; timing (the local build had no optimisation flags and
the machine was shared); container stereo metadata; fisheye on a real lens;
homography on a tilted plane; forward-motion parallax; why exactly shake_noisy
degrades without cuts; audio; Windows/MSVC; CI's OpenCV 4.10.

## Work in progress branches (pushed, no PR yet, not reviewed)

- `claude/samn2-auto-tip-box` (package 2): any-axis FindTip, activity region,
  `tests/engine/tip_scenes.h`, `engine/tools/samn_tip_bench.cpp`.
- `claude/samn2-bench-truth` (package 1): `bench_score.h`, `golden_clip.h`,
  `samn_goldenclip.cpp`, `bench_score_test.cpp`.
Both are first drafts that did not finish building and testing; continue from
the branch, do not trust them.

## How the work is organised

Independent review of every PR by a second agent with fresh data (authors
overfit). Merge only with four green CI jobs; Windows is decisive. The Owner
asked on 17 Nov to stop using the Fable model for this work and to go back to
Opus; and he is switching the Claude account.
