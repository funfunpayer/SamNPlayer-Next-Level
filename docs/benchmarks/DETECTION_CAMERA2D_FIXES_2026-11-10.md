# 2-D camera compensation: review fixes, 10-11 Nov 2026

Follow-up to [`DETECTION_CAMERA2D_2026-11-08.md`](DETECTION_CAMERA2D_2026-11-08.md) (#201), after two independent reviews. "Before" is `main` with #201 merged. Synthetic scenes only. Raw rows: [`DETECTION_CAMERA2D_FIXES_2026-11-10_before.csv`](DETECTION_CAMERA2D_FIXES_2026-11-10_before.csv) (main) and [`DETECTION_CAMERA2D_FIXES_2026-11-10.csv`](DETECTION_CAMERA2D_FIXES_2026-11-10.csv) (this change), 28 scenes each, built with the same bench code.

**Result in one line:** the gap filling the first review asked for (carry the step of a gated frame, a 3 px fallback) was built, measured and **removed again**. It fixes a steady noisy pan but makes shake, parallax and abrupt stops drift, and no safe variant was found (below). What remains are guards: they change no number of the bench, and they fix what the tests pin.

## What this change does

- **Plausibility bounds on the camera estimate** (`CameraMotion::Plausible`, `engine/src/camera_motion.cpp`). An estimate is not usable when:
  - its matrix is not finite;
  - it zooms by more than 20 % or turns by more than 0.2 rad in one frame;
  - it moves the picture's centre by more than a quarter of the picture.
- **The self-check's camera trust c6 is the 3 px share again.** That is the share `c6_min` 0.4 was calibrated on; #201 recorded the 1 px share. The change is in `engine/src/track_box.cpp`: with `record_checks` the camera estimate also fits at 3 px from the same features (`EstimateCameraFits(..., want_coarse = recorder)`), and the recorder gets that share. Without recording nothing extra is fitted. `self_check.cpp` and `generate.cpp` are unchanged.
- **A held box keeps its compensated position** (`HoldLostPositions`). A frame flagged `kLostTracker` repeats the tracker's last box; before, the compensation then moved it against the camera. Not across a scene cut: the first frame of a new scene keeps its own value.
- **A frame without a trusted 1 px fit still adds nothing, as on main.** The camera step of such a frame is lost, which is the known limit below.
- **Counters:** `TrackStats::camera_frames_gated` (new) counts frames whose 1 px fit existed but was not trusted. `camera_frames_lost` keeps its meaning: frames without a trusted step. `samn_bench` reports `camera_gated_share` and `camera_unmeasured_share`. Track cache version 8.
- **Bench:**
  - nine camera scenes (below);
  - a position-error metric `pos_err_p95_px`: p95 over frames of |track motion since the first frame - the disc's true motion in the scene| on the chosen axis;
  - `WriteCameraScene` gets a frame size, blur, noise, JPEG, an occlusion window and a near parallax layer, plus the `Shake()` and `StopGo()` camera paths. The earlier scenes' output is byte-identical.
- **Comment fix:** `TrackResult::xs / ys` are summed from steps in each frame's own pixels; under a zoom nothing is rescaled to the scene's first frame.

## Tried and rejected: filling gated frames

All variants were measured on the same binary (one run per scene; the pipeline is deterministic, spread 0), on the scenes with gated frames. "Round 1" is what the first review led to: the 3 px fallback when its share is 0.7 or more and its step lies within 1 px of the last one, else the last step for 0.5 s, then halved per frame. The "consistent" variants carry only when the last K measured steps lie within tau of their mean, for at most M frames, then nothing. "Interpolation" fills a gap of up to G frames afterwards, linearly between the measured steps on both sides.

Track r:

| scene | main (= this change) | round 1 | consistent K4, 0.5 px, M3 | consistent K6, 0.4 px, M2 | consistent + guarded 3 px (0.5 px, at most 2 in a row) | interpolation G3 | interpolation G12 |
|---|---:|---:|---:|---:|---:|---:|---:|
| pan_noisy (28 % gated) | 0.942 | 0.999 | 0.991 | 0.977 | 0.995 | 0.989 | 1.000 |
| pan_lowres_noisy (16 %) | 0.976 | 0.999 | 0.978 | 0.974 | 0.984 | 0.999 | 0.998 |
| shake_noisy (25 %) | 0.993 | **0.980** | 0.993 | 0.993 | 0.993 | **0.985** | **0.985** |
| parallax_noisy (1 %) | 0.995 | 0.989 | 0.990 | 0.989 | 0.990 | 0.990 | 0.990 |
| stop_noisy (49 %) | 0.768 | **0.561** | **0.623** | **0.706** | **0.601** | **0.669** | **0.538** |

Position error p95, px:

| scene | main | round 1 | consistent K4 | consistent K6 | consistent + 3 px | interp. G3 | interp. G12 |
|---|---:|---:|---:|---:|---:|---:|---:|
| pan_noisy | 17.4 | 4.4 | 6.9 | 10.6 | 5.9 | 9.9 | 1.6 |
| pan_lowres_noisy | 7.9 | 2.8 | **14.9** | **12.4** | **13.4** | 2.6 | 4.2 |
| shake_noisy | 11.8 | **16.4** | 11.8 | 11.8 | 11.8 | **13.8** | **13.8** |
| parallax_noisy | 7.8 | **12.2** | **11.6** | **11.6** | **11.8** | **11.6** | **11.6** |
| stop_noisy | 57.0 | **101.4** | **75.6** | 53.5 | **84.2** | **65.2** | **108.0** |

Script r follows: round 1 on `shake_noisy` 0.989 -> 0.981 and `stop_noisy` 0.877 -> 0.847; interpolation on `shake_noisy` 0.965. Bold marks a value that breaks the rule against main: track r more than 0.01 lower, or a position error more than 10 % higher. **Every variant breaks it on at least two scenes**, so none is kept.

Why: a gated frame carries no information about its own step. Adding nothing is right when the motion does not persist (shake, a camera that has stopped). Carrying or interpolating is right when it does (a steady pan). A single frame cannot tell the two apart. `parallax_noisy` has only 1 % gated frames, yet every fill made it worse. The likely cause, not verified frame by frame: those frames sit where the 1 px fit flips between the near and the far layer, so their neighbours are poor guides. The reviewer's scenes (its own generator and seeds) showed the same, e.g. under shake with 85 % gated: script r 0.975 -> 0.915.

## Bench: main -> this change (28 scenes, 5 timed runs after 3 warm-ups each)

**Every quality row is identical** on every scene: script r, r min, track r, track r min, peak error, extrema, amplitude, cuts, lost share, jumps and position error. The scenes with camera motion:

| scene | what | script r | track r | pos p95 px | gated |
|---|---|---:|---:|---:|---:|
| pan_x | the baseline's X pan | 0.9886 | 0.9992 | 2.8 | 0.00 |
| pan_x_fast | +-45 px at 0.25 Hz | 0.9881 | 0.9991 | 4.2 | 0.00 |
| zoom | 1 -> 1.4 | 0.9886 | 0.9961 | 12.2 | 0.00 |
| pan_zoom | pan + zoom | 0.9850 | 0.9844 | 9.5 | 0.00 |
| pan_noisy | sparse texture, blur 5, noise sigma 25, JPEG 20 | 0.9883 | 0.9418 | 17.4 | 0.28 |
| pan_lowres_noisy | 192x144, blur 3, noise 20, JPEG 20 | 0.9876 | 0.9755 | 7.9 | 0.16 |
| pan_fg | under a large moving textured disc | 0.9521 | 0.8456 | 27.4 | 0.00 |
| pan_lowres_fg | 192x144 under the disc, degraded | 0.9744 | 0.8042 | 26.0 | 0.00 |
| shake | hand shake +-6 / +-4 px, 4-9 Hz | 0.9860 | 0.9923 | 10.8 | 0.00 |
| shake_noisy | the same, degraded like pan_noisy | 0.9887 | 0.9930 | 11.8 | 0.25 |
| parallax | pan with a near layer moving 1.6x | 0.9466 | 0.9391 | 30.6 | 0.00 |
| parallax_noisy | the same, blur 3, noise 20, JPEG 20 | 0.9871 | 0.9953 | 7.8 | 0.01 |
| stop_noisy | 40 px/s for 1.5 s, stop 1.5 s, degraded | 0.8767 | 0.7679 | 57.0 | 0.49 |

Medians over the 27 scored scenes: script r 0.986, track r 0.993, peak error 10.0 ms, all unchanged.

**Acceptance rule:**

| rule | result |
|---|---|
| 1. no r drops by more than max(0.01, spread) | holds (nothing moves) |
| position error p95 no more than 10 % worse | holds (nothing moves) |
| 2. median r up, or peak error down by 2 ms or more | **not met: the bench does not move** |
| 3. cost | see Cost |

The guards fire only where the tests put them: implausible fits, recording, held boxes during a camera move. None of these happens in a bench scene often enough to change a number.

## Tests

Two tests-first commits, each shown failing before its fix:

- **Round 1**, on main with #201: 4 of 17 camera cases failed. The c6 share median was 0.51 (needs 0.7). Fits of zoom 1.3 / 0.75, a turn of 0.3 rad and a NaN matrix counted as usable. 92 of 92 held frames moved.
- **Round 2**, on the round-1 code: `shake under gating gains no invented steps` failed on its single noise draw with r 0.948 (needed 0.975) and position error p95 42.2 px (needed 25).

Changed on purpose, with the reason:

- **Noisy-pan test:** it asked for r 0.99, which only gap filling reached. It now pins the known limit.
- **Recording test:** it compares the positions with and without recording, instead of an absolute r.
- **`CameraSteps` unit test:** removed with the class.
- **Gated scenes are scored over noise draws** (seeds 1 to 5, median), not on one draw, because of the platform dependence below. The thresholds, with the medians and ranges measured here:

| case | median r here (range) | median p95 here | threshold | round 1 (carry + 3 px), median |
|---|---|---|---|---|
| noisy pan 320 px | 0.946 (0.923-0.965) | - | r >= 0.90 | 0.999 (it is good at this one) |
| noisy pan 192 px | 0.990 (0.955-0.994) | - | r >= 0.94 | - |
| shake + noise | 0.973 (0.948-0.990) | 27.1 px | r >= 0.93, p95 <= 40 | 0.861, 53.1 px: fails |
| abrupt stop + noise | 0.964 (0.946-0.975) | 23.6 px | r >= 0.92, p95 <= 35 | - |

New: a held position does not reach across a scene cut. All 21 camera cases pass; the full engine suite is in the PR.

## Platform dependence of the gated scenes

The CI runners failed the first version of two tests with numbers different from this machine. The scenes and seeds were the same, and so were the OS (Ubuntu 24.04), OpenCV (4.6.0+dfsg-13.1ubuntu1) and GCC 13.3:

| scene, noise draw 1234 | this machine (Xeon, AVX-512) | CI runner (Release and ASan alike) |
|---|---|---|
| noisy pan 320 px, raw r (no compensation) | 0.681188 | 0.680558 |
| noisy pan 320 px, compensated r | 0.970318 | 0.932699 |
| shake + noise, r / p95 | 0.98427 / 15.77 px | 0.938361 / 25.68 px |

- **Not the build.** An ASan + UBSan RelWithDebInfo build here (the CI flags) gives exactly this machine's unoptimised numbers. On CI, Release and ASan agree with each other.
- **The input differs.** The raw track differs already, so the input pictures differ between the machines. The likely source is the CPU-dispatched SIMD code in the mp4v encoder and in OpenCV. Not verified: the runner's CPU is not in the log, and `OPENCV_CPU_DISABLE` did not change the local result.
- **Gating amplifies it.** The difference is small where nothing is gated: a clean pan over four noise draws gives r 0.9990 to 0.9992. Where a quarter or more of the frames are gated, which frames are gated depends on the exact pixels, and every gated frame loses its camera step. One noise draw then moves r by up to 0.05 and position error p95 by up to 4x (table above); a different machine acts like a different draw.
- **Not caused by this change.** Main and this change give the same r and p95 on every one of eight noise draws of both scenes (one run each), so main has the same dependence. Main itself was not run on CI.
- **For the bench:** the rows of `pan_noisy`, `pan_lowres_noisy`, `shake_noisy`, `stop_noisy` (and in part `parallax_noisy`) hold for this machine only. On another machine they move like another noise draw. The main-vs-change identity holds per draw. The ungated scenes are stable.

## Cost

Interleaved ABBA `--timing-only` runs (main, this change, this change, main), 5 timed runs after 3 warm-ups per scene each, on all 28 scenes. Load averages (1 / 5 / 15 min) at the start and end of each run:

| run | start | end | sum of ms/frame |
|---|---|---|---:|
| main 1 | 2.17 / 3.56 / 3.80 | 1.33 / 1.58 / 1.84 | 827.5 |
| this 1 | 1.33 / 1.58 / 1.84 | 1.75 / 1.76 / 1.73 | 832.0 |
| this 2 | 1.75 / 1.76 / 1.73 | 1.53 / 1.71 / 1.70 | 819.0 |
| main 2 (after a restart of the machine) | 0.92 / 0.46 / 0.18 | 1.68 / 1.73 / 1.97 | 798.9 |

- **Mean:** main 813.2, this change 825.5 ms/frame summed: **+1.5 %**, inside the 10 % rule. Per scene the mean of the two runs moves between -12 % (`pan_fg`) and +14 % (`drop30`). That is run-to-run spread on a shared machine. Per frame the code differs only by the plausibility check, a few multiplications; the 3 px fit runs only while the self-check records, and the bench does not record.
- **RSS:** within 1.3 % on every scene.

## Remaining limits

- **A steady pan on a noisy picture drifts.** A gated frame loses its step: `pan_noisy` track r 0.942 (28 % gated), `pan_lowres_noisy` 0.976. Filling the gaps fixes this scene but breaks shake, parallax and stops (above). A safe fix needs information the single frame lacks, for example a fit over several frames (frame k-2 to k).
- **Shake under gating** is safe only because gated frames add nothing. Every gated shake step is lost, so the error grows with the gated share. The reviewer's scene with 85 % gated: script r 0.975 on main.
- **Results on gated scenes are chaotic in the input:** one noise draw, or one machine, moves r by up to 0.05 (Platform dependence).
- **Parallax:** the 1 px fit follows whichever layer has more inliers and can flip between them. `parallax` (no noise, nothing gated): track r 0.939, position error p95 30.6 px. This is a drift of the trusted fit itself, which no gate sees.
- **A foreground with as many features as the background** wins the trusted fit (`pan_lowres_fg` 0.804).
- **False cuts** from fast pans and moving foregrounds (`pan_fg`: 25 in 12 s) reset the compensation. That belongs to PR-3.
- **Not verified:** real clips (the Owner gate through `samn_compare`), Windows / MSVC, the self-check's verdicts on real clips.
