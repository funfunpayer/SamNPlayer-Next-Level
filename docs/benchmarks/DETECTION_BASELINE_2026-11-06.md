# Detection baseline, 6 Nov 2026

The first run of `samn_bench` (`engine/tools/samn_bench.cpp`) on the engine at `33a5723` (main), before any change to the detection. Nothing here is a target: it is the line every later change is compared against, by the acceptance rule below. Raw rows: [`DETECTION_BASELINE_2026-11-06.csv`](DETECTION_BASELINE_2026-11-06.csv) (scene, metric, value).

- Machine: 4 cores (shared VM), Linux 6.18, unoptimised build (empty `CMAKE_BUILD_TYPE`), so ms/frame is high in absolute terms; compare only builds of the same kind. OpenCV threads inside the engine put the loadavg at 2 to 2.5 even when nothing else runs.
- Command: `samn_bench --out <dir>` (5 timed runs after 3 warm-ups per scene; the 300 s scene is measured once). Quality rows come from this run; the ms/frame rows were repeated three times (`--timing-only`, the second and third full runs).
- Load average (1 / 5 / 15 min): run 1 (about 33 min) started at 3.76 / 5.08 / 4.34 (right after a build: not quiet) and ended at 1.39 / 1.92 / 2.86; run 2 started at 0.70 / 0.52 / 0.22 and ended at 2.35 / 2.51 / 2.42; run 3 ended at 2.15 / 2.26 / 2.22.
- Metric definitions: `samn_bench --help`. "track r" is the same r on the raw box-centre track before the signal chain; "r" is on the final script (Recommended profile, no tip hint). `-` = not defined for the scene (no occlusion mask, no truth motion).

## Quality and cost (run 1)

| scene | r med | r min | r spread | track r | peak med ms | peak p95 ms | ext err | amp | false cuts | lost | loss P | loss R | jumps | ms/frame | RSS MB |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| clean | 0.989 | 0.988 | 0.000 | 0.999 | 10.0 | 27.0 | 0 | 0.96 | 0 | 0.00 | - | - | 0 | 34.59 | 68 |
| pan_y | 0.988 | 0.988 | 0.000 | 0.999 | 10.0 | 30.0 | 0 | 0.96 | 0 | 0.05 | - | - | 0 | 34.82 | 67 |
| pan_x | 0.989 | 0.987 | 0.000 | 0.797 | 10.0 | 27.0 | 0 | 0.95 | 0 | 0.00 | - | - | 0 | 25.03 | 64 |
| zoom | 0.988 | 0.987 | 0.000 | 0.948 | 10.0 | 27.0 | 0 | 0.96 | 0 | 0.00 | - | - | 0 | 42.26 | 79 |
| occl_0.5s | 0.976 | 0.976 | 0.000 | 0.992 | 10.0 | 30.0 | 0 | 0.96 | 0 | 0.03 | 1.00 | 1.00 | 0 | 35.59 | 68 |
| occl_2s | 0.772 | 0.769 | 0.000 | 0.835 | 10.0 | 30.0 | -4 | 0.94 | 0 | 0.12 | 1.00 | 1.00 | 0 | 39.02 | 77 |
| occl_4s | 0.716 | 0.712 | 0.000 | 0.542 | 10.0 | 30.0 | -6 | 0.93 | 0 | 0.25 | 1.00 | 1.00 | 0 | 30.60 | 72 |
| blur | 0.978 | 0.973 | 0.000 | 1.000 | 15.0 | 15.0 | -1 | 0.91 | 0 | 0.00 | - | - | 0 | 33.84 | 67 |
| noise_jpeg | 0.989 | 0.988 | 0.000 | 0.999 | 10.0 | 27.0 | 0 | 0.96 | 0 | 0.00 | - | - | 0 | 35.90 | 77 |
| drop30 | 0.962 | 0.960 | 0.000 | 0.955 | 20.0 | 70.0 | 0 | 0.83 | 4 | 0.00 | - | - | 0 | 33.92 | 68 |
| distract_anti | 0.989 | 0.989 | 0.000 | 0.999 | 10.0 | 27.0 | 0 | 0.96 | 0 | 0.00 | - | - | 0 | 34.25 | 67 |
| distract_quarter | 0.988 | 0.988 | 0.000 | 0.999 | 10.0 | 30.0 | 0 | 0.95 | 0 | 0.00 | - | - | 0 | 34.29 | 66 |
| hard_cut | 0.988 | 0.988 | 0.000 | 0.999 | 10.0 | 30.0 | 0 | 0.96 | 0 | 0.00 | - | - | 0 | 33.96 | 68 |
| light_cut_jump | 0.662 | 0.598 | 0.000 | 0.705 | 10.0 | 54.0 | -11 | 0.68 | 0 | 0.50 | - | - | 0 | 28.71 | 67 |
| fast_small | 0.977 | 0.972 | 0.000 | 0.999 | 0.0 | 0.0 | -1 | 0.99 | 0 | 0.50 | - | - | 0 | 24.04 | 59 |
| still | - | - | - | - | - | - | - | - | 0 | 0.00 | - | - | 0 | 32.51 | 66 |
| drift_300s | 0.977 | 0.965 | 0.000 | 0.999 | 10.0 | 11.0 | 0 | 1.76 | 0 | 0.00 | - | - | 0 | 29.17 | 67 |

## ms/frame, three full runs

| scene | run 1 | run 2 | run 3 | median | RSS MB (1/2/3) |
|---|---:|---:|---:|---:|---:|
| clean | 34.59 | 26.14 | 27.46 | 27.46 | 67.7/66.5/67.3 |
| pan_y | 34.82 | 30.24 | 27.36 | 30.24 | 66.6/66.7/66.9 |
| pan_x | 25.03 | 28.03 | 27.75 | 27.75 | 63.7/63.2/63.3 |
| zoom | 42.26 | 32.95 | 31.93 | 32.95 | 79.5/78.7/79.4 |
| occl_0.5s | 35.59 | 31.99 | 26.98 | 31.99 | 67.7/67.1/67.4 |
| occl_2s | 39.02 | 37.09 | 30.06 | 37.09 | 76.7/76.2/76.2 |
| occl_4s | 30.60 | 29.04 | 25.45 | 29.04 | 71.8/72.3/71.2 |
| blur | 33.84 | 32.77 | 28.27 | 32.77 | 67.0/66.7/66.5 |
| noise_jpeg | 35.90 | 33.23 | 28.09 | 33.23 | 77.0/75.7/76.2 |
| drop30 | 33.92 | 27.57 | 29.75 | 29.75 | 68.0/67.7/67.8 |
| distract_anti | 34.25 | 29.47 | 27.70 | 29.47 | 66.7/66.0/65.7 |
| distract_quarter | 34.29 | 31.92 | 26.69 | 31.92 | 66.5/67.2/67.3 |
| hard_cut | 33.96 | 28.10 | 26.13 | 28.10 | 68.4/67.0/67.1 |
| light_cut_jump | 28.71 | 25.70 | 29.95 | 28.71 | 66.7/66.2/65.9 |
| fast_small | 24.04 | 22.94 | 22.62 | 22.94 | 59.2/58.9/58.9 |
| still | 32.51 | 26.33 | 31.17 | 31.17 | 66.3/65.6/65.1 |
| drift_300s | 29.17 | 26.44 | 28.37 | 28.37 | 67.3/65.9/65.5 |

The spread between repetitions is up to 25 % on the same scene (shared machine, unoptimised build), above the 10 % cost rule: judge a cost change on the median of at least three runs, ideally on a quiet machine. RSS is stable within 1.5 MB.

## What the baseline shows

- **Pan, X stroke: the raw track reproduces the defect, the script does not.** `pan_x` track r 0.797 (worst window 0.787) against `pan_y` 0.999 and `clean` 0.999: the camera compensation shifts only y, so an X stroke carries the camera's x pan. The script r is 0.989 for both, because the default detrend high-passes this slow synthetic pan (0.11 Hz) away. The 0.81 from the analysis therefore holds for the track, not for a Recommended script on this scene; a faster pan will not be saved by the detrend. Judge PR-2 (2-D compensation) on `track r` of `pan_x` and `zoom` (track r 0.948).
- **Occlusion costs what it hides.** 0.5 s: r 0.976; 2 s: 0.772; 4 s: 0.716 (script extrema 4 and 6 short of the truth). The tracker's lost flag finds every hidden frame (precision and recall 1.00) and flags nothing else.
- **Dropped frames (30 % repeated) raise false scene cuts: 4**, and the peak error doubles (median 20 ms, p95 70 ms); r 0.962.
- **Lighting cut with the disc 120 px away: r 0.662** (worst window 0.598). The cut is found (1, at the right frame), but the tracker is flagged lost for the whole second half (lost share 0.50), 11 extrema are missing and only 54 % of the keys are matched.
- **fast_small (256x144, 2.5 Hz): half the frames flagged lost**, yet r 0.977.
- Peak timing: median 10 ms, p95 27-30 ms on the clean scenes (the 25 fps frame is 40 ms).
- No jump over 60 px in any scene, not even the lighting-cut scene whose disc jumps 120 px (the track keeps its box).
- `amp` of `drift_300s` (1.76) is not a defect: the truth is normalised over the whole clip, the drift widens that range, the script is detrended per stroke. Read that row for r and timing only.
- An occluded disc's truth keeps its sine; peak timing and amplitude use only the visible key times.

## Scenes

| scene | what |
|---|---|
| clean | 1 Hz disc, amp 80, textured background |
| pan_y | camera pan, Y stroke (WritePanning) |
| pan_x | camera pan, X stroke, axis X |
| zoom | camera zoom 1.0 -> 1.4, Y stroke |
| occl_0.5s | disc hidden for 0.5 s from 6 s |
| occl_2s | disc hidden for 2.0 s from 6 s |
| occl_4s | disc hidden for 4.0 s from 6 s |
| blur | 2 Hz disc, amp 110, full-frame motion blur |
| noise_jpeg | noise sigma 8, JPEG quality 20, then mp4v |
| drop30 | 30 % of the frames repeated (dropped) |
| distract_anti | second disc, anti-phase |
| distract_quarter | second disc, quarter phase |
| hard_cut | hard cut to another background at 6 s |
| light_cut_jump | lighting cut at 6 s, the disc 120 px further right |
| fast_small | 256x144, 30 fps, 2.5 Hz, no cut |
| still | a still disc: the engine must refuse (no motion) |
| drift_300s | 300 s, 1 Hz stroke on a +-25 px, 90 s drift |

All scenes are 320x240 at 25 fps and 12 s unless stated (occlusion 16 s, `still` 6 s, `fast_small` 256x144 at 30 fps, `drift_300s` 300 s). Dropped frames are frames repeated by the writer (OpenCV cannot write a variable frame rate). The ground-truth key times of every scene are written to `clips/<scene>_truth.csv` in the output folder; the clips themselves are deleted unless `--keep-clips`.

## Acceptance rule

A change passes when:

1. no scene's r drops by more than max(0.01, its 5-run spread of r);
2. the median r rises, or the median peak error falls by 2 ms or more (over the scenes);
3. ms/frame rises by 10 % or less and peak RSS by 10 % or less (median of runs, see the note on spread);
4. before a change becomes a default: the Owner gate through `samn_compare` on his private clips (numbers only; no media in any repository).

Reproduce: `build-app/engine/samn_bench --out <dir>` (about 33 min); one scene: `samn_bench --scenes pan_x --out <dir>`. ctest runs one scene as `samn_bench_smoke` (`--quick --no-track --scenes clean --require-r 0.9`).
