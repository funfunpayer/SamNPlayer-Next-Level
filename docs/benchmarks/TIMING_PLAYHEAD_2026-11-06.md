# Timing bench, video playhead extrapolation after the review fixes, 6 Nov 2026

The video rows of `tests/app/timing_bench` before the video playhead extrapolation and with it after the review fixes of PR #163 (see [`../TIMING.md`](../TIMING.md) § 7). Mean, p50, p95 and p99 are the median of the three repetitions' values, "min (all)" and "max (all)" the extremes of all three, the last column is each repetition's p99. All times in ms unless noted. The first "after" runs (5 Nov, before the review fixes) are in [`TIMING_PLAYHEAD_2026-11-05.md`](TIMING_PLAYHEAD_2026-11-05.md).

- Machine: 4 cores (shared VM), Linux 6.18, Qt 6.4.2 (Ubuntu packages), `QT_QPA_PLATFORM=offscreen`, `QT_MEDIA_BACKEND=ffmpeg`, unoptimised build (empty `CMAKE_BUILD_TYPE`, assertions on). 5 plays of the 4 s test-pattern clip per scenario, UDP TCode stand-in.
- **before:** the unchanged code (`origin/main` 34fbd36 with the bench of PR #159), `--only "media,media +load3"`, 5 Nov 20:33 to 20:36 UTC (the same runs as the 5 Nov file).
- **after:** this branch with the review fixes, `--only "media raw,media raw +load3,media,media +load3"`, 6 Nov 07:52 to 07:57 UTC. `media raw` is the report-only path, run in the same repetitions for comparison; `media` is what the Player sends (the reports and the estimate between them).
- "extra sends within a move": writes on the stroke channel after a move went out and before the move to the next point was due (the same move again, or a move back). The bench of the "before" runs did not count them, nor the "move shorter than scripted" row.

| scenario | metric (ms unless noted) | reps | n (all) | mean | min (all) | p50 | p95 | p99 | max (all) | p99 per rep |
|---|---|---|---|---|---|---|---|---|---|---|
| before: media | report interval | 3 | 570 | 100.00 | 83.35 | 100.32 | 100.49 | 104.30 | 115.83 | 103.22 / 104.53 / 104.30 |
| before: media | media clock behind a linear clock | 3 | 585 | 0.56 | 0.00 | 0.51 | 0.99 | 4.81 | 16.84 | 3.74 / 4.81 / 4.84 |
| before: media | PlaybackOutput::update (us) | 3 | 585 | 50.99 | 20.14 | 40.28 | 96.20 | 145.19 | 2011.71 | 85.72 / 419.08 / 145.19 |
| before: media | L0 report -> wire | 3 | 120 | 0.54 | 0.17 | 0.52 | 0.65 | 2.17 | 5.04 | 5.04 / 1.25 / 2.17 |
| before: media | L0 move start - due | 3 | 120 | 46.63 | 4.20 | 43.20 | 89.77 | 89.81 | 96.92 | 89.60 / 96.92 / 89.81 |
| before: media | L0 move end - due | 3 | 120 | 0.64 | -1.96 | 0.49 | 1.22 | 5.04 | 8.85 | 5.00 / 8.85 / 5.04 |
| before: media | L0 move end - due, offset ignored | 3 | 120 | 0.64 | -1.96 | 0.49 | 1.22 | 5.04 | 8.85 | 5.00 / 8.85 / 5.04 |
| before: media | L0 duration - formula | 3 | 120 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 / 0.00 / 0.00 |
| before: media +load3 | report interval | 3 | 566 | 101.56 | 91.23 | 100.26 | 100.86 | 195.60 | 200.42 | 195.60 / 199.39 / 103.49 |
| before: media +load3 | media clock behind a linear clock | 3 | 581 | 37.71 | 0.00 | 0.83 | 99.72 | 103.36 | 104.03 | 104.01 / 103.36 / 3.89 |
| before: media +load3 | PlaybackOutput::update (us) | 3 | 581 | 107.86 | 16.92 | 40.87 | 229.26 | 590.67 | 11669.91 | 4249.02 / 590.67 / 285.76 |
| before: media +load3 | L0 report -> wire | 3 | 120 | 0.30 | 0.18 | 0.24 | 0.41 | 1.98 | 4.76 | 4.76 / 1.98 / 0.33 |
| before: media +load3 | L0 move start - due | 3 | 120 | 46.09 | 4.19 | 41.37 | 89.04 | 89.57 | 89.62 | 89.62 / 89.52 / 89.57 |
| before: media +load3 | L0 move end - due | 3 | 120 | 0.08 | -92.01 | 0.08 | 1.00 | 3.15 | 12.78 | 12.78 / 3.15 / 0.86 |
| before: media +load3 | L0 move end - due, offset ignored | 3 | 120 | 0.08 | -92.01 | 0.08 | 1.00 | 3.15 | 12.78 | 12.78 / 3.15 / 0.86 |
| before: media +load3 | L0 duration - formula | 3 | 120 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 / 0.00 / 0.00 |
| media raw | report interval | 3 | 570 | 100.01 | 93.31 | 100.28 | 100.41 | 101.32 | 106.90 | 101.01 / 101.32 / 101.86 |
| media raw | media clock behind a linear clock | 3 | 585 | 0.52 | 0.00 | 0.49 | 0.97 | 1.67 | 7.27 | 1.53 / 1.67 / 1.86 |
| media raw | PlaybackOutput::update (us) | 3 | 585 | 41.84 | 19.30 | 32.43 | 72.16 | 103.14 | 612.65 | 101.42 / 103.14 / 114.81 |
| media raw | L0 report -> wire | 3 | 120 | 0.45 | 0.22 | 0.40 | 0.57 | 1.80 | 2.83 | 1.80 / 0.59 / 2.83 |
| media raw | L0 move start - due | 3 | 120 | 46.48 | 4.31 | 41.58 | 89.65 | 90.33 | 91.06 | 91.06 / 89.79 / 90.33 |
| media raw | L0 move end - due | 3 | 120 | 0.46 | -0.24 | 0.35 | 1.08 | 1.74 | 3.00 | 1.74 / 1.24 / 3.00 |
| media raw | L0 move end - due, offset ignored | 3 | 120 | 0.46 | -0.24 | 0.35 | 1.08 | 1.74 | 3.00 | 1.74 / 1.24 / 3.00 |
| media raw | L0 duration - formula | 3 | 120 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 / 0.00 / 0.00 |
| media raw | L0 move shorter than scripted (%) | 3 | 120 | 10.91 | 1.19 | 9.38 | 20.37 | 20.37 | 20.37 | 20.37 / 20.37 / 20.37 |
| media raw +load3 | report interval | 3 | 570 | 100.53 | 84.24 | 100.22 | 100.57 | 107.15 | 199.48 | 107.15 / 115.73 / 102.60 |
| media raw +load3 | media clock behind a linear clock | 3 | 585 | 19.50 | 0.00 | 0.62 | 100.41 | 100.65 | 102.66 | 100.68 / 100.65 / 2.81 |
| media raw +load3 | PlaybackOutput::update (us) | 3 | 585 | 63.49 | 7.05 | 31.83 | 202.93 | 606.22 | 4390.05 | 606.22 / 4140.01 / 232.03 |
| media raw +load3 | L0 report -> wire | 3 | 120 | 0.39 | 0.15 | 0.21 | 1.24 | 3.72 | 4.52 | 3.15 / 4.52 / 3.72 |
| media raw +load3 | L0 move start - due | 3 | 120 | 46.28 | 4.04 | 42.60 | 88.85 | 89.43 | 93.48 | 89.43 / 93.48 / 89.41 |
| media raw +load3 | L0 move end - due | 3 | 120 | 0.37 | -2.58 | 0.21 | 2.52 | 3.62 | 4.57 | 3.62 / 4.57 / 2.98 |
| media raw +load3 | L0 move end - due, offset ignored | 3 | 120 | 0.37 | -2.58 | 0.21 | 2.52 | 3.62 | 4.57 | 3.62 / 4.57 / 2.98 |
| media raw +load3 | L0 duration - formula | 3 | 120 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 / 0.00 / 0.00 |
| media raw +load3 | L0 move shorter than scripted (%) | 3 | 120 | 10.91 | 1.19 | 9.38 | 20.37 | 20.37 | 20.37 | 20.37 / 20.37 / 20.37 |
| media | report interval | 3 | 570 | 100.00 | 96.16 | 100.19 | 100.53 | 100.79 | 103.75 | 102.58 / 100.79 / 100.79 |
| media | media clock behind a linear clock | 3 | 585 | 0.49 | 0.00 | 0.48 | 0.96 | 1.26 | 4.36 | 2.96 / 1.26 / 1.07 |
| media | PlaybackOutput::update (us) | 3 | 585 | 16.98 | 5.58 | 13.90 | 43.01 | 64.81 | 442.66 | 125.10 / 63.83 / 64.81 |
| media | L0 report -> wire | 3 | 120 | 0.36 | 0.24 | 0.35 | 0.48 | 0.54 | 0.74 | 0.74 / 0.54 / 0.45 |
| media | L0 move start - due | 3 | 120 | 5.51 | 0.47 | 4.47 | 12.51 | 13.36 | 16.01 | 12.64 / 16.01 / 13.36 |
| media | L0 move end - due | 3 | 120 | 0.43 | -1.13 | 0.26 | 1.23 | 3.19 | 4.37 | 4.37 / 3.19 / 1.47 |
| media | L0 move end - due, offset ignored | 3 | 120 | 0.43 | -1.13 | 0.26 | 1.23 | 3.19 | 4.37 | 4.37 / 3.19 / 1.47 |
| media | L0 duration - formula | 3 | 120 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 / 0.00 / 0.00 |
| media | L0 move shorter than scripted (%) | 3 | 120 | 1.26 | 0.00 | 1.19 | 2.75 | 2.97 | 3.20 | 2.75 / 3.20 / 2.97 |
| media +load3 | report interval | 3 | 569 | 100.53 | 96.57 | 100.09 | 100.75 | 103.61 | 199.33 | 103.66 / 101.65 / 103.61 |
| media +load3 | media clock behind a linear clock | 3 | 584 | 18.85 | 0.00 | 0.59 | 96.57 | 96.74 | 102.09 | 96.74 / 1.74 / 102.04 |
| media +load3 | PlaybackOutput::update (us) | 3 | 584 | 18.17 | 4.27 | 13.69 | 35.70 | 200.08 | 2040.03 | 200.08 / 206.21 / 198.76 |
| media +load3 | L0 report -> wire | 3 | 120 | 0.22 | 0.14 | 0.17 | 0.26 | 0.62 | 4.07 | 0.62 / 4.07 / 0.37 |
| media +load3 | L0 move start - due | 3 | 120 | 5.38 | 0.05 | 4.58 | 12.70 | 13.99 | 17.18 | 13.99 / 12.73 / 17.18 |
| media +load3 | L0 move end - due | 3 | 120 | 0.06 | -2.14 | 0.13 | 0.95 | 3.65 | 4.86 | 1.34 / 4.86 / 3.65 |
| media +load3 | L0 move end - due, offset ignored | 3 | 120 | 0.06 | -2.14 | 0.13 | 0.95 | 3.65 | 4.86 | 1.34 / 4.86 / 3.65 |
| media +load3 | L0 duration - formula | 3 | 120 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 / 0.00 / 0.00 |
| media +load3 | L0 move shorter than scripted (%) | 3 | 120 | 1.28 | 0.00 | 1.37 | 2.97 | 2.97 | 4.15 | 2.97 / 2.97 / 4.15 |

| scenario | loadavg (1 min) start -> end, per repetition | extra sends within a move, per repetition |
|---|---|---|
| before: media | 2.06 -> 2.33 / 3.19 -> 2.16 / 2.40 -> 1.58 | not counted (older bench) |
| before: media +load3 | 2.33 -> 3.19 / 2.16 -> 2.40 / 1.58 -> 1.98 | not counted (older bench) |
| media raw | 1.97 -> 1.41 / 2.09 -> 1.38 / 1.81 -> 1.42 | 0 / 0 / 0 |
| media raw +load3 | 1.41 -> 2.03 / 1.38 -> 1.84 / 1.42 -> 2.23 | 0 / 0 / 0 |
| media | 2.03 -> 1.53 / 1.84 -> 1.35 / 2.23 -> 1.96 | 0 / 0 / 0 |
| media +load3 | 1.53 -> 2.09 / 1.35 -> 1.88 / 1.96 -> 2.46 | 0 / 0 / 0 |
