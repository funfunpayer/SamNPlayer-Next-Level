# 2-D camera compensation: review fixes, 10 Nov 2026

Follow-up to [`DETECTION_CAMERA2D_2026-11-08.md`](DETECTION_CAMERA2D_2026-11-08.md) (#201). It fixes the findings of the independent review of #201. "Before" is `main` with #201 (the 1 px fit, a gated frame adds nothing); "after" is this branch. Synthetic scenes only. Quality is unchanged by the newer `main` merged into this branch (frame-reader time fix #240): `clean`, `hard_cut` and the two noisy scenes were re-run on the merged tree and match in every quality row. Raw rows: [`DETECTION_CAMERA2D_FIXES_2026-11-10.csv`](DETECTION_CAMERA2D_FIXES_2026-11-10.csv) (after, all 23 scenes).

## What changed

- **A gated frame no longer drops its camera step** (major 1). Before, a frame whose 1 px fit had an inlier share under 0.5 added 0, so every gated frame lost its whole step for good and a real pan came back as a drift (noisy 320 px pan: 84 of 300 frames gated, track r 0.942). `CameraSteps` (`engine/src/camera_motion.cpp`) now decides per frame:
  1. the 1 px fit when it is usable (as before);
  2. else the 3 px fit from the same matched features, if it is plausible, its share is 0.7 or more, and its step at the tip lies within 1 px of the last measured step. The agreement check stops a foreground that moves 1 to 3 px per frame from taking over through the looser fit;
  3. else the last measured step, carried for 0.5 s of samples, then halved every frame. At a cut it starts from rest.
- **The self-check's camera trust c6 is the 3 px share again** (major 2). With `record_checks` the 3 px fit is computed on every frame and its share is recorded, the share that `c6_min` 0.4 was calibrated on. Without recording, the 3 px fit runs only on gated frames.
- **Plausibility bounds** (minor 3): an estimate is not usable when its matrix is not finite, it zooms by more than 20 % or turns by more than 0.2 rad in one frame, or it moves the picture's centre by more than a quarter of the picture.
- **A held box holds its compensated position** (minor 4). Frames flagged `kLostTracker` repeat the tracker's last box. Before, the compensation subtracted the camera from that repeated frame position, so the held tip moved against the camera, in all 92 held frames of the test. Now the compensated position repeats too.
- `TrackStats::camera_frames_gated` (new) counts frames whose 1 px fit was not trusted. `camera_frames_lost` now counts frames without a measured step (carried or none). `samn_bench` reports both as `camera_gated_share` and `camera_unmeasured_share`. Track cache version 8.
- The comment on `TrackResult::xs / ys` is corrected: steps are summed in each frame's own pixels; under a zoom nothing is rescaled to the scene's first frame.
- `WriteCameraScene` gets a frame size, a disc radius, blur, noise, JPEG and an occlusion window. The 320x240 default output is byte-identical.

## Tests first

`9980967` adds the tests alone; `0950b53` adds the fix. On `main` (with #201) the new tests fail like this, 4 of the 17 camera cases and 9 assertions:

```
camera_motion_test.cpp:404: ERROR: CHECK( R(comp, s) >= 0.99 )   320 px: raw r 0.681188, compensated r 0.970318, frames without a step 40 of 150
camera_motion_test.cpp:404: ERROR: CHECK( R(comp, s) >= 0.99 )   192 px: raw r 0.682524, compensated r 0.989599, frames without a step 31 of 150
camera_motion_test.cpp:421: ERROR: CHECK( Median(shares) >= 0.7 ) median recorded share 0.514851
camera_motion_test.cpp:422: ERROR: CHECK( R(r, s) >= 0.99 )
camera_motion_test.cpp:438: ERROR: CHECK_FALSE( fit(1.3, 0).Usable() )
camera_motion_test.cpp:439: ERROR: CHECK_FALSE( fit(0.75, 0).Usable() )
camera_motion_test.cpp:440: ERROR: CHECK_FALSE( fit(1, 0.3).Usable() )
camera_motion_test.cpp:443: ERROR: CHECK_FALSE( broken.Usable() )
camera_motion_test.cpp:464: ERROR: CHECK( moved == 0 )            92 of 92 held frames moved
[doctest] test cases:  17 |  13 passed | 4 failed
```

After the fix, all 20 camera cases pass (three more: the shift bound, `CameraSteps` carry / fade / agreement, and the gated count). On the 6 s test clips:

| case | before | after |
|---|---:|---:|
| noisy pan 320x240 (sparse texture, blur 5, noise sigma 25, JPEG 20) | 0.970, 40 frames without a step | 0.9996, 0 |
| noisy pan 192x144 (blur 3, noise sigma 20, JPEG 20) | 0.990, 31 frames without a step | 0.9992, 3 |
| recorded camera share (c6), noisy pan, median | 0.51 (1 px) | 0.82 (3 px) |
| held frames that move | 92 of 92 | 0 of 92 |
| pan + large moving foreground (cuts off) | 0.998493 | 0.998493 (unchanged) |

Every earlier camera case gives the same value as before.

## Bench (one full run, 5 timed runs after 3 warm-ups per scene)

The 19 earlier scenes are **identical to 4 decimals** in every quality row (r, r min, track r, peak error, extrema, amplitude, cuts, lost, jumps) to the #201 run: none of them has a gated frame (`camera_gated_share` 0.00). The new scenes:

| scene | what | track r: pre-#201 | #201 | the fix at 3 px | **after** | script r: #201 -> after | gated | unmeasured |
|---|---|---:|---:|---:|---:|---|---:|---:|
| pan_noisy | `pan_x_fast` on a sparse texture, blur 5, noise sigma 25, JPEG 20 | 0.669 | 0.942 | 0.998 | **0.999** | 0.988 -> 0.988 | 0.28 | 0.00 |
| pan_lowres_noisy | 192x144, pan +-27 px at 0.25 Hz, blur 3, noise sigma 20, JPEG 20 | 0.671 | 0.976 | 0.996 | **0.999** | 0.988 -> 0.988 | 0.16 | 0.02 |
| pan_fg | pan +-40 px at 0.3 Hz under the large moving textured disc | 0.690 | 0.846 | 0.805 | **0.843** | 0.952 -> 0.951 | 0.00 | 0.00 |
| pan_lowres_fg | 192x144, the same under the disc, sparse texture, blur 5, noise 4, JPEG 30 | 0.672 | 0.804 | 0.894 | **0.804** | 0.974 -> 0.974 | 0.00 | 0.00 |

"Pre-#201" is the engine before the 2-D compensation (y only). "The fix at 3 px" is this branch built with a 3 px first fit, measured only to answer "where 3 px had it". Track r min after: 0.999, 0.999, 0.843, 0.789 (CSV).

- **Noisy and small pictures:** fixed. Track r is 0.999 where the 3 px fit had 0.996 to 0.998. Most gated frames are filled by the agreeing 3 px fit; 2 % stay without a measured step on the 192 px scene.
- **`pan_fg` (rule 1 holds: -0.003):** the bench runs with scene cuts on, and the moving textured disc gives **25 false cuts** in 12 s. Each one resets the compensation and skips the tracker update, which keeps this scene at 0.84 with any camera estimate (the test without cuts: 0.998). This is the false-cut problem of the first doc and belongs to PR-3.
- **`pan_lowres_fg`: a remaining limit, plainly.** At 192x144 under the moving disc the 1 px fit is trusted (share 0.5 or more, none gated) but follows the foreground part of the time. The 3 px variant was better here (0.894 against 0.804), though not at 0.99. A foreground with as many usable features as the background wins any single-frame fit, and no inlier statistic tells it from the background (first doc, "Found on the way"). The fix neither causes nor cures it: #201 had 0.804 too. Recovering it needs a different signal (temporal consistency of the background over several frames, or a foreground mask); not done.

Acceptance rule: rule 1 (no r drops by more than max(0.01, spread)) holds on every scene; the largest drop is `pan_fg` -0.003 (track r) and -0.0012 (script r). Rule 2: over the 22 scored scenes the median track r rises 0.9974 -> 0.9990, and the median script r 0.9863 -> 0.9865. Rule 3: see Cost.

## Cost

Two `--timing-only` runs of each engine, interleaved (#201, this branch, #201, this branch), 5 timed runs after 3 warm-ups per scene. Load averages (1 / 5 / 15 min): run 1 #201 1.70 -> 3.44 (other work started during it, up to about 8), this branch -> 1.49; run 2 #201 0.81 -> 4.17, this branch -> 1.55.

| run | sum of ms/frame over 23 scenes, #201 -> this branch | largest RSS change on a scene |
|---|---|---:|
| 1 | 867.7 -> 715.3 (-17.6 %) | 0.8 % |
| 2 | 748.8 -> 690.6 (-7.8 %) | 1.3 % |

Every scene was faster after in the mean of the two runs (-2 % to -25 %). That is the load, not the change: #201 ran first in each pair, while other work was busier. What the change adds is a second RANSAC on the same points, only on gated frames, or on every frame when the self-check records. So the honest reading is **no measurable cost increase** (rule 3 holds) and no speed-up claimed. RSS is within 1.3 % on every scene.

## Remaining limits

- A foreground with as many features as the background (`pan_lowres_fg`): above.
- False cuts on fast pans and moving foregrounds (`pan_fg`: 25): PR-3 (confirmed cuts).
- The carry assumes the camera keeps its motion for up to 0.5 s. A camera that stops inside a gated stretch is over-compensated by up to 0.5 s of its last speed.
- The agreement check compares with the last measured step. After a long gated stretch with a changed camera speed, the 3 px fit may never agree again until the 1 px fit is trusted.
- Not verified: real clips (the Owner gate through `samn_compare`), Windows / MSVC, the self-check's verdicts on real clips (c6 now sees the 3 px share again, as before #201).
