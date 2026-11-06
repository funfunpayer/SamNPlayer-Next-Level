# Timing bench, video playhead extrapolation, 5 Nov 2026

The video rows of `tests/app/timing_bench` before and after the video playhead extrapolation (see [`../TIMING.md`](../TIMING.md) § 7). Aggregated as in [`TIMING_2026-11-05.md`](TIMING_2026-11-05.md): mean, p50, p95 and p99 are the median of the three repetitions' values, "max (all)" is the largest value of all three, the last column is each repetition's p99. All times in ms unless noted.

- Machine: 4 cores (shared VM; another agent's build ran on it at times), Linux 6.18, Qt 6.4.2 (Ubuntu packages), `QT_QPA_PLATFORM=offscreen`, `QT_MEDIA_BACKEND=ffmpeg`, unoptimised build (empty `CMAKE_BUILD_TYPE`, assertions on). 5 plays of the 4 s test-pattern clip per scenario, UDP TCode stand-in.
- **before:** the unchanged code (`origin/main` 34fbd36 with the bench of PR #159), `--only "media,media +load3"`, 20:33 to 20:36 UTC.
- **after:** this branch, `--only "media raw,media raw +load3,media,media +load3"`, 20:59 to 21:04 UTC. `media raw` is the report-only path (as before) run in the same repetition for comparison; `media` is what the Player now sends (the reports and the estimate between them). The row "move shorter than scripted (%)" is new in the bench (100 x (1 - sent duration / scripted interval)); the unchanged bench of the "before" runs has no such row.
- In the "after" runs, "L0 report -> wire" counts from the position that sent the move (a report or a tick), the "due" times follow the media clock's own reports (as in § 3 of `TIMING.md`).

| scenario | metric (ms unless noted) | reps | n (all) | mean | p50 | p95 | p99 | max (all) | p99 per rep |
|---|---|---|---|---|---|---|---|---|---|
| before: media | report interval | 3 | 570 | 100.00 | 100.32 | 100.49 | 104.30 | 115.83 | 103.22 / 104.53 / 104.30 |
| before: media | media clock behind a linear clock | 3 | 585 | 0.56 | 0.51 | 0.99 | 4.81 | 16.84 | 3.74 / 4.81 / 4.84 |
| before: media | PlaybackOutput::update (us) | 3 | 585 | 50.99 | 40.28 | 96.20 | 145.19 | 2011.71 | 85.72 / 419.08 / 145.19 |
| before: media | L0 report -> wire | 3 | 120 | 0.54 | 0.52 | 0.65 | 2.17 | 5.04 | 5.04 / 1.25 / 2.17 |
| before: media | L0 move start - due | 3 | 120 | 46.63 | 43.20 | 89.77 | 89.81 | 96.92 | 89.60 / 96.92 / 89.81 |
| before: media | L0 move end - due | 3 | 120 | 0.64 | 0.49 | 1.22 | 5.04 | 8.85 | 5.00 / 8.85 / 5.04 |
| before: media | L0 move end - due, offset ignored | 3 | 120 | 0.64 | 0.49 | 1.22 | 5.04 | 8.85 | 5.00 / 8.85 / 5.04 |
| before: media | L0 duration - formula | 3 | 120 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 / 0.00 / 0.00 |
| before: media +load3 | report interval | 3 | 566 | 101.56 | 100.26 | 100.86 | 195.60 | 200.42 | 195.60 / 199.39 / 103.49 |
| before: media +load3 | media clock behind a linear clock | 3 | 581 | 37.71 | 0.83 | 99.72 | 103.36 | 104.03 | 104.01 / 103.36 / 3.89 |
| before: media +load3 | PlaybackOutput::update (us) | 3 | 581 | 107.86 | 40.87 | 229.26 | 590.67 | 11669.91 | 4249.02 / 590.67 / 285.76 |
| before: media +load3 | L0 report -> wire | 3 | 120 | 0.30 | 0.24 | 0.41 | 1.98 | 4.76 | 4.76 / 1.98 / 0.33 |
| before: media +load3 | L0 move start - due | 3 | 120 | 46.09 | 41.37 | 89.04 | 89.57 | 89.62 | 89.62 / 89.52 / 89.57 |
| before: media +load3 | L0 move end - due | 3 | 120 | 0.08 | 0.08 | 1.00 | 3.15 | 12.78 | 12.78 / 3.15 / 0.86 |
| before: media +load3 | L0 move end - due, offset ignored | 3 | 120 | 0.08 | 0.08 | 1.00 | 3.15 | 12.78 | 12.78 / 3.15 / 0.86 |
| before: media +load3 | L0 duration - formula | 3 | 120 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 / 0.00 / 0.00 |
| media raw | report interval | 3 | 570 | 100.01 | 100.32 | 100.49 | 101.41 | 102.88 | 101.41 / 102.85 / 101.11 |
| media raw | media clock behind a linear clock | 3 | 585 | 0.53 | 0.53 | 0.98 | 1.53 | 3.26 | 1.53 / 3.18 / 1.34 |
| media raw | PlaybackOutput::update (us) | 3 | 585 | 55.67 | 42.69 | 103.57 | 148.67 | 443.11 | 253.13 / 148.67 / 128.69 |
| media raw | L0 report -> wire | 3 | 120 | 0.60 | 0.54 | 0.68 | 2.07 | 2.20 | 0.94 / 2.07 / 2.20 |
| media raw | L0 move start - due | 3 | 120 | 46.63 | 41.79 | 89.83 | 89.96 | 90.56 | 89.88 / 90.56 / 89.96 |
| media raw | L0 move end - due | 3 | 120 | 0.63 | 0.51 | 1.25 | 2.85 | 3.09 | 2.85 / 3.09 / 2.22 |
| media raw | L0 move end - due, offset ignored | 3 | 120 | 0.63 | 0.51 | 1.25 | 2.85 | 3.09 | 2.85 / 3.09 / 2.22 |
| media raw | L0 duration - formula | 3 | 120 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 / 0.00 / 0.00 |
| media raw | L0 move shorter than scripted (%) | 3 | 120 | 10.91 | 9.38 | 20.37 | 20.37 | 20.37 | 20.37 / 20.37 / 20.37 |
| media raw +load3 | report interval | 3 | 567 | 100.54 | 100.29 | 100.54 | 104.04 | 200.69 | 102.88 / 104.04 / 199.65 |
| media raw +load3 | media clock behind a linear clock | 3 | 582 | 18.65 | 0.61 | 100.17 | 100.79 | 103.88 | 3.71 / 103.88 / 100.79 |
| media raw +load3 | PlaybackOutput::update (us) | 3 | 582 | 63.56 | 42.85 | 107.58 | 417.53 | 2167.48 | 417.53 / 1573.60 / 332.11 |
| media raw +load3 | L0 report -> wire | 3 | 120 | 0.31 | 0.25 | 0.49 | 1.59 | 5.14 | 1.59 / 5.14 / 0.43 |
| media raw +load3 | L0 move start - due | 3 | 120 | 46.50 | 41.72 | 89.46 | 94.36 | 126.02 | 89.57 / 94.36 / 126.02 |
| media raw +load3 | L0 move end - due | 3 | 120 | 0.32 | 0.25 | 1.25 | 4.51 | 5.14 | 2.25 / 5.14 / 4.51 |
| media raw +load3 | L0 move end - due, offset ignored | 3 | 120 | 0.32 | 0.25 | 1.25 | 4.51 | 5.14 | 2.25 / 5.14 / 4.51 |
| media raw +load3 | L0 duration - formula | 3 | 120 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 / 0.00 / 0.00 |
| media raw +load3 | L0 move shorter than scripted (%) | 3 | 120 | 10.91 | 9.38 | 20.37 | 20.37 | 20.37 | 20.37 / 20.37 / 20.37 |
| media | report interval | 3 | 570 | 100.00 | 99.81 | 100.79 | 101.10 | 143.82 | 100.98 / 101.10 / 102.68 |
| media | media clock behind a linear clock | 3 | 585 | 0.51 | 0.51 | 0.95 | 1.45 | 44.23 | 1.45 / 1.41 / 2.96 |
| media | PlaybackOutput::update (us) | 3 | 585 | 22.05 | 17.89 | 58.26 | 79.33 | 166.16 | 79.33 / 80.06 / 79.19 |
| media | L0 report -> wire | 3 | 120 | 0.59 | 0.48 | 1.22 | 2.38 | 4.05 | 0.65 / 4.05 / 2.38 |
| media | L0 move start - due | 3 | 120 | 5.57 | 4.58 | 12.71 | 12.74 | 12.76 | 12.74 / 12.61 / 12.76 |
| media | L0 move end - due | 3 | 120 | 0.57 | 0.45 | 1.48 | 3.57 | 44.57 | 1.45 / 3.57 / 44.57 |
| media | L0 move end - due, offset ignored | 3 | 120 | 0.57 | 0.45 | 1.48 | 3.57 | 44.57 | 1.45 / 3.57 / 44.57 |
| media | L0 duration - formula | 3 | 120 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 / 0.00 / 0.00 |
| media | L0 move shorter than scripted (%) | 3 | 120 | 1.25 | 1.19 | 2.75 | 2.97 | 2.97 | 2.75 / 2.97 / 2.97 |
| media +load3 | report interval | 3 | 570 | 100.00 | 99.91 | 100.76 | 101.37 | 200.23 | 102.10 / 101.02 / 101.37 |
| media +load3 | media clock behind a linear clock | 3 | 585 | 0.52 | 0.51 | 0.96 | 1.98 | 102.60 | 100.76 / 1.27 / 1.98 |
| media +load3 | PlaybackOutput::update (us) | 3 | 585 | 20.67 | 16.61 | 43.94 | 239.96 | 5225.97 | 233.16 / 290.39 / 239.96 |
| media +load3 | L0 report -> wire | 3 | 120 | 0.35 | 0.22 | 0.27 | 5.44 | 5.92 | 5.92 / 5.44 / 0.30 |
| media +load3 | L0 move start - due | 3 | 120 | 5.36 | 5.14 | 12.28 | 12.75 | 15.69 | 12.74 / 15.69 / 12.75 |
| media +load3 | L0 move end - due | 3 | 120 | 0.42 | 0.16 | 1.12 | 5.20 | 5.48 | 5.20 / 5.48 / 1.23 |
| media +load3 | L0 move end - due, offset ignored | 3 | 120 | 0.42 | 0.16 | 1.12 | 5.20 | 5.48 | 5.20 / 5.48 / 1.23 |
| media +load3 | L0 duration - formula | 3 | 120 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 / 0.00 / 0.00 |
| media +load3 | L0 move shorter than scripted (%) | 3 | 120 | 1.29 | 1.19 | 2.75 | 2.97 | 3.66 | 2.97 / 3.66 / 2.97 |

| scenario | loadavg (1 min) start -> end, per repetition |
|---|---|
| before: media | 2.06 -> 2.33 / 3.19 -> 2.16 / 2.40 -> 1.58 |
| before: media +load3 | 2.33 -> 3.19 / 2.16 -> 2.40 / 1.58 -> 1.98 |
| media raw | 1.24 -> 0.82 / 1.53 -> 1.01 / 1.82 -> 1.30 |
| media raw +load3 | 0.82 -> 1.44 / 1.01 -> 1.69 / 1.20 -> 1.71 |
| media | 1.44 -> 0.95 / 1.69 -> 1.21 / 1.71 -> 1.13 |
| media +load3 | 0.95 -> 1.53 / 1.21 -> 1.82 / 1.13 -> 1.66 |
