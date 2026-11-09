# 2-D camera compensation, 8 Nov 2026

Detection plan PR-2, judged by `samn_bench` against the baseline ([`DETECTION_BASELINE_2026-11-06.md`](DETECTION_BASELINE_2026-11-06.md)). Raw rows: [`DETECTION_CAMERA2D_2026-11-08_before.csv`](DETECTION_CAMERA2D_2026-11-08_before.csv) (the old engine, with the two new scenes) and [`DETECTION_CAMERA2D_2026-11-08_after.csv`](DETECTION_CAMERA2D_2026-11-08_after.csv). Synthetic scenes only.

## What changed

- **The estimate:** `EstimateCameraMotion` (`engine/src/camera_motion.cpp`) returns the whole similarity of `estimateAffinePartial2D` (shift, scale, rotation) and its RANSAC inlier share. Before, `CameraMotionY` returned only `t(1,2)`, the y motion of pixel (0, 0), and only ys were shifted. Same features, same single pass per frame, full-size picture (REJ-24).
- **Where it is measured:** at the tip's centre in the previous frame, `m * p - p`. A pan moves every scene point by the shift; a zoom or a rotation moves a point by its distance from their centre. That centre is neither the frame centre nor the image origin: the estimate itself places it (its shift is the motion of pixel (0, 0); for a zoom about the frame centre that is `(1 - s) * c`, which the old code subtracted from a tip that sat at the centre and did not move: the drift that took `zoom` to 0.948). The tip moves with the scene, so the scene's motion at the tip is what to remove.
- **Accumulation:** per frame into `camera_dx` / `camera_dy` like the old `camera_dy`: reset to 0 at every scene cut, smoothed per scene (Savitzky-Golay 9/2), subtracted from `xs` and `ys`. `frame_xs` (new) and `frame_ys` keep the frame pixels for the tip path. No reset at a re-anchor without a cut: a re-anchor finds the tip again in the same scene, whose camera motion stays valid; a reset there would put a step of the whole accumulated pan into the track.
- **Inlier gate:** an estimate with an inlier share under 0.5 adds nothing (REJ-09 / GD-14: 0.24 on noise against 0.84-0.90 on background). Measured at the new 1 px distance (6 s clips): noise background median 0.066 (0.15 at 3 px), still camera 1.0, pans and zoom 0.93 or more; under the large moving foreground (the background holds about 60 % of the features) median 0.63, min 0.48, so 1 of 150 frames is gated there: a foreground of half the picture brings correct estimates close to the gate.
- **RANSAC distance 1 px** (was OpenCV's 3 px). At 3 px a foreground that moves 1 to 3 px per frame against the background joins the background's model, the fit is a compromise and the inlier share stays high, so the gate cannot see it: a pan under a large moving textured disc tracked at r 0.91 with 3 px and 0.998 with 1 px (accumulated camera error over 6 s: -20.5 px against 4.0 px). LK features are sub-pixel; no pan scene lost accuracy (r 0.9991 either way). The tip finder's `CameraMotionY` keeps 3 px (unchanged).
- **Axis choice:** on the compensated x and y (before: raw x against compensated y). A vertical stroke under a horizontal pan of +-80 px chose x before (test `the axis is chosen on the compensated motion`). Risk: a bad compensation could now also flip the choice the other way; the gate and the robust 2-98 % spread are the guard.
- **Zoom and amplitude:** the compensation removes motion, it does not rescale: under a zoom the stroke keeps the frame's scale, so an amplitude that swings with the zoom caps r (pan + zoom +-25 %: about 0.985). Rescaling to the first frame would multiply any scale bias over a long clip; the signal chain normalises the amplitude per window anyway.
- **Gating:** `TrackOptions::camera_compensation` as before (default on, off where it was off); no other default changed. Track cache version 7.

## Quality (one full run each, 5 timed runs after 3 warm-ups per scene)

Before: the old engine (this branch before the change, identical in every quality row to the committed baseline for the 17 old scenes). After: this change.

| scene | r before | r after | r min before | r min after | track r before | track r after | track r min before | track r min after | amp before | amp after | ext err before | after |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| clean | 0.989 | 0.989 | 0.988 | 0.988 | 0.999 | 0.999 | 0.999 | 0.999 | 0.96 | 0.96 | 0 | 0 |
| pan_y | 0.988 | 0.988 | 0.988 | 0.988 | 0.999 | 0.999 | 0.999 | 0.999 | 0.96 | 0.96 | 0 | 0 |
| pan_x | 0.989 | 0.989 | 0.987 | 0.988 | **0.797** | **0.999** | 0.787 | 0.999 | 0.95 | 0.96 | 0 | 0 |
| zoom | 0.988 | 0.989 | 0.987 | 0.988 | **0.948** | **0.996** | 0.943 | 0.996 | 0.96 | 0.96 | 0 | 0 |
| pan_x_fast (new) | **0.936** | **0.988** | 0.924 | 0.987 | **0.663** | **0.999** | 0.663 | 0.999 | 0.79 | 0.95 | -1 | 0 |
| pan_zoom (new) | 0.977 | 0.985 | 0.972 | 0.985 | **0.800** | **0.984** | 0.790 | 0.983 | 0.89 | 0.88 | 0 | 0 |
| occl_0.5s | 0.976 | 0.976 | 0.976 | 0.976 | 0.992 | 0.992 | 0.991 | 0.991 | 0.96 | 0.96 | 0 | 0 |
| occl_2s | 0.772 | 0.772 | 0.769 | 0.769 | 0.835 | 0.835 | 0.833 | 0.833 | 0.94 | 0.94 | -4 | -4 |
| occl_4s | 0.716 | 0.716 | 0.712 | 0.712 | 0.542 | 0.541 | 0.532 | 0.532 | 0.93 | 0.93 | -6 | -6 |
| blur | 0.978 | 0.978 | 0.973 | 0.973 | 1.000 | 1.000 | 1.000 | 1.000 | 0.91 | 0.91 | -1 | -1 |
| noise_jpeg | 0.989 | 0.989 | 0.988 | 0.988 | 0.999 | 0.999 | 0.999 | 0.999 | 0.96 | 0.96 | 0 | 0 |
| drop30 | 0.962 | 0.962 | 0.960 | 0.960 | 0.955 | 0.955 | 0.951 | 0.951 | 0.83 | 0.83 | 0 | 0 |
| distract_anti | 0.989 | 0.989 | 0.989 | 0.988 | 0.999 | 0.999 | 0.999 | 0.999 | 0.96 | 0.96 | 0 | 0 |
| distract_quarter | 0.988 | 0.989 | 0.988 | 0.988 | 0.999 | 0.999 | 0.999 | 0.999 | 0.95 | 0.95 | 0 | 0 |
| hard_cut | 0.988 | 0.988 | 0.988 | 0.988 | 0.999 | 0.999 | 0.999 | 0.999 | 0.96 | 0.96 | 0 | 0 |
| light_cut_jump | 0.661 | 0.666 | 0.598 | 0.603 | **0.705** | **0.689** | 0.641 | 0.618 | 0.68 | 0.66 | -11 | -11 |
| fast_small | 0.977 | 0.977 | 0.972 | 0.972 | 0.999 | 0.999 | 0.999 | 0.999 | 0.99 | 0.99 | -1 | -1 |
| still | - | - | - | - | - | - | - | - | - | - | - | - |
| drift_300s | 0.977 | 0.977 | 0.965 | 0.965 | 0.999 | 0.999 | 0.998 | 0.998 | 1.76 | 1.76 | 0 | 0 |

r spread over the 5 runs is 0.000 on every scene (the pipeline is deterministic). Peak error (median 10 ms, p95 27-30 ms), false cuts, lost share and jumps are the same in every scene before and after (CSV). `still` refuses (no motion) before and after.

Over the 18 scenes with r: median r 0.9775 -> 0.9865, median track r 0.9951 -> 0.9990; median peak error 10.0 -> 10.0 ms.

## Acceptance rule

1. **No scene's r drops by more than max(0.01, its spread):** script r: no drop anywhere (the largest is `distract_anti` r min -0.0005). Track r: **`light_cut_jump` drops 0.705 -> 0.689 (min 0.641 -> 0.618), more than 0.01. Rule 1 fails on this one track row.** Cause, measured: after the lighting cut the tracker sits on the background at the old place (lost share 0.50) while the disc strokes 120 px to the right, outside the excluded box. Its edge features entered the old 3 px estimate, so the old "camera" correction of the held track correlated 0.71 with the disc's own stroke and lent the held track part of the stroke. With 1 px the correction is uncorrelated with it (-0.04) and the track is what the tracker saw: windowed r 0.761 / 0.617 against 0.755 / 0.619 with the compensation off. The script r of the scene rises (0.661 -> 0.666). The scene's defect is the lost tracker (PR-3), not the camera; restoring the leak would undo the foreground fix.
2. **Median r rises:** 0.9775 -> 0.9865 (track 0.9951 -> 0.9990). Targets: `pan_x` track r 0.999 (>= 0.99), `zoom` 0.996 (>= 0.98).
3. **Cost:** see below.
4. **Owner gate** (`samn_compare` on the private clips): not run; required before the next default change, and wanted for this one, which changes a default path.

**Scenes without camera motion:** `clean`, `pan_y`, `hard_cut`, `blur`, `drop30`, `fast_small`, `drift_300s`, `occl_0.5s/2s` are identical in every row to 4 decimals. Not identical: `noise_jpeg` r min 0.9881 -> 0.9882, `distract_anti` r 0.9889 -> 0.9886 (min 0.9888 -> 0.9883, track min 0.9988 -> 0.9987), `distract_quarter` r 0.9884 -> 0.9886 (min 0.9876 -> 0.9880), `occl_4s` track r 0.5418 -> 0.5414. A still camera's estimate is not exactly the identity (mp4v noise: up to 0.0024 px and 1e-5 scale per frame on `clean`); the old code subtracted its y shift, the new one its 1 px fit at the tip, so the last digit can move (largest 0.0005, under the rule's 0.01). Byte-identical tracks would need a dead band around the identity, which would also swallow a real slow pan; not done. The test `without camera motion the compensation changes nothing that matters` bounds the change on `clean` at 0.05 px (measured 0.0045 px).

## Cost

Three `--timing-only` runs of each engine, interleaved before / after / before / after ... (5 timed runs after 3 warm-ups per scene each), so that load from other work on the shared machine falls on both. ms/frame is the median of the 5 runs; the table shows the three repetitions and their median.

| run | load average at start (1 / 5 / 15 min) | at end | sum of ms/frame, before / after |
|---|---|---|---|
| 1 | before 5.34 / 4.16 / 2.00 (4 fuzzers of other work running), after 4.42 / 5.65 / 5.34 | 2.57 / 2.40 / 2.96 | 585.3 / 475.4 (-18.8 %) |
| 2 | before 2.57 / 2.40 / 2.96, after 4.00 / 3.72 / 3.30 (one fuzzer of other work) | 2.70 / 2.92 / 3.18 | 517.2 / 514.6 (-0.5 %) |
| 3 | before 5.13 / 13.52 / 13.68 (other work just ended; the first attempt of this run hit a time limit and was repeated), after 1.92 / 1.93 / 4.92 | 1.82 / 1.75 / 2.53 | 471.8 / 454.7 (-3.6 %) |

| scene | before ms/frame (1/2/3) | median | after ms/frame (1/2/3) | median | change | RSS MB before / after (median) |
|---|---:|---:|---:|---:|---:|---:|
| clean | 30.91/23.89/33.39 | 30.91 | 25.13/29.42/23.12 | 25.13 | -18.7 % | 67.1 / 67.0 (-0.1 %) |
| pan_y | 31.43/26.05/32.31 | 31.43 | 25.62/31.06/24.02 | 25.62 | -18.5 % | 66.6 / 66.7 (+0.2 %) |
| pan_x | 27.92/25.99/23.98 | 25.99 | 25.27/27.02/21.94 | 25.27 | -2.8 % | 62.7 / 62.8 (+0.2 %) |
| zoom | 37.21/29.42/27.31 | 29.42 | 27.92/34.25/27.50 | 27.92 | -5.1 % | 78.8 / 78.6 (-0.3 %) |
| pan_x_fast | 29.59/24.48/22.16 | 24.48 | 23.73/27.14/23.05 | 23.73 | -3.1 % | 62.9 / 62.9 (+0.0 %) |
| pan_zoom | 32.65/28.87/23.60 | 28.87 | 24.89/35.08/25.44 | 25.44 | -11.9 % | 73.9 / 74.0 (+0.1 %) |
| occl_0.5s | 29.87/23.05/22.91 | 23.05 | 23.33/25.00/24.10 | 24.10 | +4.6 % | 67.2 / 67.1 (-0.1 %) |
| occl_2s | 33.99/28.38/26.44 | 28.38 | 27.87/31.88/27.93 | 27.93 | -1.6 % | 75.9 / 75.7 (-0.3 %) |
| occl_4s | 28.82/24.92/23.09 | 24.92 | 25.22/24.24/22.47 | 24.24 | -2.7 % | 71.9 / 72.1 (+0.3 %) |
| blur | 31.96/26.08/25.27 | 26.08 | 27.56/26.18/25.15 | 26.18 | +0.4 % | 66.3 / 66.5 (+0.3 %) |
| noise_jpeg | 31.64/25.72/24.64 | 25.72 | 25.41/32.42/24.83 | 25.41 | -1.2 % | 76.0 / 76.2 (+0.3 %) |
| drop30 | 33.79/28.86/23.98 | 28.86 | 25.97/27.46/23.60 | 25.97 | -10.0 % | 67.6 / 67.9 (+0.4 %) |
| distract_anti | 30.83/29.11/24.15 | 29.11 | 22.81/24.57/21.88 | 22.81 | -21.6 % | 65.7 / 65.7 (+0.0 %) |
| distract_quarter | 32.11/33.61/22.44 | 32.11 | 24.33/23.29/21.88 | 23.29 | -27.5 % | 67.3 / 66.9 (-0.6 %) |
| hard_cut | 27.49/30.45/23.44 | 27.49 | 22.54/24.74/24.18 | 24.18 | -12.0 % | 67.0 / 67.1 (+0.1 %) |
| light_cut_jump | 28.33/28.56/23.33 | 28.33 | 23.85/23.28/24.58 | 23.85 | -15.8 % | 66.2 / 66.2 (+0.0 %) |
| fast_small | 27.13/24.11/20.18 | 24.11 | 21.85/20.18/20.78 | 20.78 | -13.8 % | 59.0 / 58.7 (-0.5 %) |
| still | 30.12/27.96/24.22 | 27.96 | 26.84/23.33/23.97 | 23.97 | -14.3 % | 65.4 / 65.6 (+0.3 %) |
| drift_300s | 29.51/27.69/24.96 | 27.69 | 25.22/24.02/24.24 | 24.24 | -12.5 % | 65.7 / 65.9 (+0.3 %) |
| **all (sum of medians)** | | 524.9 | | 470.1 | -10.4 % | 68.1 / 68.1 mean |

Run 1 of the old engine ran under four busy processes of other work, so the medians favour the new engine; the fair pairs are runs 2 and 3: -0.5 % and -3.6 %. **No measurable cost increase** (the estimate is the same single feature pass; only a 2x3 matrix is used instead of one element, and RANSAC at 1 px), well inside the 10 % rule and inside the 25 % spread between repetitions of one scene. **RSS unchanged** (within 0.6 % on every scene). The quality runs (`_before.csv` / `_after.csv`) also carry ms/frame rows; they ran at different loads (before 3.00 -> 2.24, during builds; after 1.81 -> 8.38) and are not used for the cost judgement.

## Found on the way (not fixed here)

- **The fixed scene-cut test fires on fast pans.** On this texture `IsSceneCut` (signature difference > 12 on a 64x48 picture) starts at a camera speed of about 3 px per frame, 1 % of the frame width per frame: a pan of +-60 px at 0.25 Hz gave 10 false cuts in 6 s, +-60 px at 0.5 Hz 89 cuts in 150 frames. Every cut resets the compensation and skips the tracker update, so no compensation can work there. The large moving foreground also gives 8 false cuts in 6 s with cuts on (the test runs it with cuts off). `pan_x_fast` is therefore +-45 px at 0.25 Hz (2.8 px per frame, 0 cuts); the tests run the 0.5 Hz pans with cuts off. A cut confirmed by the camera estimate (a frame whose background is still tracked is no cut) belongs to PR-3.
- **A foreground larger than the background's share of the features** (sparse background, dense moving disc) wins the estimate with a high inlier share; no per-frame statistic can tell it from the background. Measured: background density 0.4 boxes per 1000 px^2 under the disc of radius 95: accumulated error -22 px over 6 s at 1 px, -29 px at 3 px. Not covered by any gate.
- Baseline weaknesses unchanged by this PR: `light_cut_jump` stays lost for the whole second half (r 0.66), occlusion 2 s / 4 s r 0.77 / 0.72, 30 % dropped frames give 4 false cuts.

## Scenes added

| scene | what |
|---|---|
| pan_x_fast | camera pan x +-45 px at 0.25 Hz (sub-pixel), X stroke, axis X; the 3 s default detrend window removes only about 30 % of a 0.25 Hz pan, so the script r was 0.936 |
| pan_zoom | pan x +-40 px at 0.2 Hz with zoom 1 +- 0.25 at 0.15 Hz, Y stroke, axis auto; the tip leaves the zoom centre |

Both use `WriteCameraScene` (`tests/engine/synthetic_scenes.h`): a camera over a 760x680 textured world, `frame = zoom * (world - centre(t)) + (160, 120)`, the disc in world coordinates (GD-22), optional hard cut, noise background and moving foreground.

## Tests

`tests/engine/camera_motion_test.cpp`, 13 cases. Against the old behaviour (y shift only, no gate, 3 px, axis on raw x) 8 of the first 12 failed (16 assertions; the old behaviour emulated inside `TrackBox`, the new estimate API kept); the two estimate unit tests, `pan along y` and `without camera motion` passed already, the cache case needs the new field. Measured after (whole-clip r of the compensated track, 6 s clips, cuts and memory off unless stated):

| case | before | after |
|---|---:|---:|
| pan x 0.11 Hz +-30 (WritePanningX) | 0.850 | 0.999 |
| pan x 0.25 Hz +-45 | 0.674 | 0.999 |
| pan x 0.5 Hz +-60 | 0.546 | 0.999 |
| pan y 0.25 / 0.5 Hz | pass | 0.999 / 0.999 |
| diagonal pan 0.4 Hz +-40, x stroke | 0.713 | 0.999 (y spread 4.4 px of 80) |
| zoom ramp 1 -> 1.4 (tip at the zoom centre) | 0.927 (raw 0.994) | 0.994 |
| zoom 1 +- 0.2 at 0.3 Hz | 0.863 | 0.988 |
| pan + zoom, axis auto | 0.781 | 0.982 |
| noise background (gate) | 0.090 (raw 0.999) | 0.999 (>= 90 % of frames gated) |
| pan + large moving foreground | 0.677 | 0.998 |
| hard cut during a pan (cuts and memory on) | 0.600, step 44 px | 0.9998, step 0.5 px |
| y stroke under x pan +-80, axis auto | axis x (wrong) | axis y, 0.999 |
| x stroke under y pan +-80, axis auto | pass | axis x, 0.999 |

Machine: 4 cores shared with other work (load averages per run in the timing table); unoptimised build (empty `CMAKE_BUILD_TYPE`).
