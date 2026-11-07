# Intensity arc: phase suggestions before and after, 6 Nov 2026

The phase rules (D-157) against the intensity arc (D-165) on synthetic clips whose intro and climaxes are known. Synthetic data only: no real clip was measured, and every calibrated constant of the arc (`kFullConfidenceProminence`, `kMinRise`, `kActionLevel`, `kEarlierStep`) was set on clips like these.

- **Tool:** `tests/bench/phase_bench.cpp`, target `phase_bench` (built with the tests, not in ctest). It keeps the old rules as `LegacyRules`, the regression oracle: the code of `core/src/phase_rules.cpp` at `33a5723`, less the script tempo and `active_share`, which no caller ever set. Both run on the same clips through the public output, the suggested phases. The bench exits with 1 when the arc finds fewer climaxes than the old rules on any scenario.
- **Clips:** a seeded LCG, so every run gives the same clips. 25 per scenario, 500 ms per step, +-15 % jitter on every sample (+-50 % for noise). Unless the scenario says otherwise: 150-600 s, a calm intro of 8-25 s, action at a base motion of 0.15-0.25 and loudness 0.25-0.4, climaxes of 10-20 s at 1.8-2.2 x the motion and loudness 0.8-0.95, and a calm tail after the last one. The scenarios after "noise" were added after a review found shapes the first set never produced: a climax that runs to the end; a clip that opens at its top; plateaus of 60-120 s; clips of 30, 45, 60 and 90 s; two 6-8 s bursts 10, 15, 20 or 25 s apart (centre to centre); a 2-3 s black cut inside the climax; a random walk of the motion under even sound; a loud, busy opening of 20-40 s; a start in the middle of the action.
- **Truth and hits:** a climax is hit when a suggested climax stretch has its midpoint within 5 s of the truth; two bursts closer than the 20 s between climaxes are one climax, hit at either burst. The truth of a build-up is 5 s before the drop (the middle of the rise's last 10 s). A clip that opens at its top has no climax there (decided in D-165). An intro is hit when its end is within 3 s; where the truth has no intro (noise, drift, an opening at the top, a loud opening, a mid-action start), when none is suggested. A false climax is a suggested climax more than 5 s from every truth.
- **CPU:** `std::clock` over 20 calls per clip, microseconds per `SuggestPhases` call. Linux 6.18, GCC 13, `-O2`, 4 cores shared with other work (loadavg 2-4). The table is the first of three runs; the "all" rows of the other two were 30 / 44 and 31 / 46 us (before / arc).

## Five fixed cases (`tests/core/intensity_arc_test.cpp`)

Error of the climax stretch's midpoint and of the intro end, in seconds (+ late, - early).

| Case | Truth | Climax before | Climax arc | Intro before | Intro arc |
|---|---|---|---|---|---|
| A climax at 30 % (37.5 s) | intro 10 s | missed (search starts at half the clip) | +0.25 | +0.5 | -1.0 |
| B loud talky intro, climax 87.5 s | intro 20 s | -1.25 | +0.25 | 0.0 | +0.5 |
| C linear build-up, top 95 s | intro 15 s | -9.75 | -3.0 | +3.0 | +0.5 |
| D two climaxes, 46 s and 98 s | intro 10 s | only 98 s (-1.0) | 0.0 and -0.25 | +0.5 | -1.0 |
| E motion peak under even music, 77.5 s | intro 10 s | -1.25 | +0.25 | +0.5 | -0.5 |

## 550 generated clips, `-O2`

| Scenario | Rules | Climax hits | Median climax error (s) | False climaxes | Intro hits | Median intro error (s) | CPU us / call |
|---|---|---|---|---|---|---|---|
| early climax (20-35 %) | before | 0/25 | - | 0 | 25/25 | 0.78 | 48 |
| early climax (20-35 %) | arc | 25/25 | 0.52 | 0 | 25/25 | 0.27 | 78 |
| late climax (75-88 %) | before | 25/25 | 1.50 | 0 | 25/25 | 0.77 | 44 |
| late climax (75-88 %) | arc | 25/25 | 0.31 | 0 | 25/25 | 0.23 | 63 |
| two climaxes | before | 25/50 | 74.66 | 0 | 25/25 | 0.63 | 46 |
| two climaxes | arc | 50/50 | 0.41 | 0 | 25/25 | 0.30 | 67 |
| linear build-up | before | 0/25 | 20.75 | 25 | 0/25 | 21.37 | 53 |
| linear build-up | arc | 20/25 | 2.38 | 5 | 25/25 | 0.30 | 77 |
| loud talky intro | before | 25/25 | 1.33 | 0 | 25/25 | 0.79 | 59 |
| loud talky intro | arc | 25/25 | 0.25 | 0 | 25/25 | 0.34 | 96 |
| flat music, motion peak | before | 0/25 | - | 0 | 25/25 | 0.84 | 50 |
| flat music, motion peak | arc | 25/25 | 0.29 | 0 | 25/25 | 0.17 | 80 |
| noise, no arc | before | 0/0 | - | 0 | 25/25 | - | 46 |
| noise, no arc | arc | 0/0 | - | 0 | 25/25 | - | 82 |
| climax runs to the end | before | 25/25 | 0.35 | 0 | 25/25 | 0.76 | 49 |
| climax runs to the end | arc | 25/25 | 0.98 | 0 | 25/25 | 0.24 | 77 |
| opens at its top (no climax) | before | 0/0 | - | 0 | 25/25 | - | 46 |
| opens at its top (no climax) | arc | 0/0 | - | 0 | 25/25 | - | 70 |
| plateau of 60-120 s | before | 1/25 | 11.37 | 16 | 25/25 | 0.50 | 49 |
| plateau of 60-120 s | arc | 25/25 | 0.26 | 0 | 25/25 | 0.39 | 76 |
| 30 s clip | before | 25/25 | 1.52 | 0 | 17/25 | 0.61 | 5 |
| 30 s clip | arc | 25/25 | 0.30 | 0 | 25/25 | 0.27 | 6 |
| 45 s clip | before | 25/25 | 0.80 | 0 | 25/25 | 0.88 | 7 |
| 45 s clip | arc | 25/25 | 0.61 | 0 | 25/25 | 0.29 | 9 |
| 60 s clip | before | 25/25 | 0.65 | 0 | 25/25 | 0.86 | 9 |
| 60 s clip | arc | 25/25 | 0.38 | 0 | 25/25 | 0.26 | 11 |
| 90 s clip | before | 25/25 | 0.47 | 0 | 25/25 | 0.52 | 13 |
| 90 s clip | arc | 25/25 | 0.32 | 0 | 25/25 | 0.28 | 15 |
| two bursts 10 s apart | before | 25/25 | 2.55 | 0 | 25/25 | 0.79 | 50 |
| two bursts 10 s apart | arc | 25/25 | 0.27 | 0 | 25/25 | 0.22 | 71 |
| two bursts 15 s apart | before | 9/25 | 5.10 | 9 | 25/25 | 0.65 | 44 |
| two bursts 15 s apart | arc | 25/25 | 0.19 | 0 | 25/25 | 0.35 | 65 |
| two bursts 20 s apart | before | 23/50 | 19.53 | 0 | 25/25 | 0.65 | 34 |
| two bursts 20 s apart | arc | 25/50 | 19.50 | 0 | 25/25 | 0.35 | 45 |
| two bursts 25 s apart | before | 23/50 | 24.20 | 0 | 25/25 | 0.81 | 33 |
| two bursts 25 s apart | arc | 49/50 | 0.24 | 0 | 25/25 | 0.19 | 49 |
| black cut inside the climax | before | 24/25 | 2.98 | 1 | 25/25 | 0.78 | 33 |
| black cut inside the climax | arc | 24/25 | 3.06 | 1 | 25/25 | 0.22 | 47 |
| slow drift, no climax | before | 0/0 | - | 0 | 25/25 | - | 30 |
| slow drift, no climax | arc | 0/0 | - | 0 | 25/25 | - | 43 |
| loud concert opening | before | 25/25 | 0.54 | 0 | 25/25 | - | 31 |
| loud concert opening | arc | 25/25 | 0.24 | 0 | 25/25 | - | 45 |
| starts mid-action | before | 21/25 | 0.23 | 0 | 25/25 | - | 33 |
| starts mid-action | arc | 25/25 | 0.44 | 0 | 25/25 | - | 51 |
| **all** | before | 351/550 | 1.38 | 51 | 517/550 | 0.76 | 37 |
| **all** | arc | 518/550 | 0.39 | 6 | 550/550 | 0.28 | 56 |

The arc finds at least as many climaxes as the old rules on every scenario. A call costs about 1.5 x the old rules: tens of microseconds, and 2.2-3.3 ms against 0.9-1.4 ms on a 2 h clip of 14 400 steps (two runs). The arc keeps about ten vectors of one double per step, around 1 MB for 2 h. The review's fuzzer (random lengths, steps, NaN / inf / huge values, options out of range; 100 000 cases plus 3 h and 10 h clips) ran clean under ASan and UBSan, with at most three climaxes and the same result twice for every input. Its 10 h square wave took 4.9 s there before the rest-of-clip median became one sort per call, and 0.15 s after.

## Slow drift

On the review's random-walk clips (200 clips of 600 s, the motion walking by +-0.5 % per step under even sound, no climax in them), the old rules suggested 23 climaxes, the first version of the arc 64, and the arc now 2. On the bench's own drift scenario, both give 0.

## Reading the table

- **Early climaxes** were never found before: the old search started at the middle of the clip.
- **The flat-music clips** failed before for a second reason: the old rules clipped motion at its 90th percentile, and a climax shorter than 10 % of the clip lies above it. The arc caps motion at 2 x that level instead.
- **Two climaxes:** the old rules report one; the arc reports others at half the primary's prominence or more, 20 s or more away. Bursts 20 s apart fuse in the 20 s arc into one climax (25 of 50 truths with both rules). Two 10 s bursts with only 15 s between them are also one climax for the arc (the review's case at 100 / 125 s; the old rules found none).
- **Plateaus** were found once in 25 before: a long plateau lifts the clip's median so far that nothing stands 1.25 x above it. The arc compares a climax with the median of the rest of the clip and walks across the plateau's top before it measures the prominence.
- **Short clips:** the arc's time scales (and the shortest intro) shrink in proportion below two minutes, so a 6 s climax of a 30 s clip is no longer smoothed away.
- **Build-ups** are where the arc still misses: 5 of 25, all early, on rises of 100-500 s whose top is barely above the 30 s before it. The intro of a build-up is found every time (the old rules: never).
- **Black cuts:** a climax is one stretch of pictures that are not empty, never cut into pieces and never merged with another climax; a top on a black picture moves to the nearest picture that is not. Between two cuts a climax can stay shorter than `climax_min_ms`.
- **Noise, drift and openings** give no climax.

Run it again: `cmake --build build --target phase_bench && build/tests/phase_bench [--variants N]`.
