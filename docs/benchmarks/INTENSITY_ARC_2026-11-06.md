# Intensity arc: phase suggestions before and after, 6 Nov 2026

The phase rules (D-157) against the intensity arc (D-165) on synthetic clips whose intro and climaxes are known. Synthetic data only: no real clip was measured.

- **Tool:** `tests/bench/phase_bench.cpp`, target `phase_bench` (built with the tests, not in ctest). It keeps the old rules as `LegacyRules` (the code of `core/src/phase_rules.cpp` at `33a5723`) and runs both on the same clips through the public output, the suggested phases.
- **Clips:** a seeded LCG, so every run gives the same clips. 25 per scenario, 150-600 s at 500 ms per step, +-15 % jitter on every sample (+-50 % for noise). Intro 8-25 s with little motion; action at a base motion of 0.15-0.25 and loudness 0.25-0.4; a climax is 10-20 s at 1.8-2.2 x the motion and loudness 0.8-0.95, followed by a calm tail when it is the last one.
- **Truth and hits:** a climax is hit when a suggested climax stretch has its midpoint within 5 s of the truth; the truth of a build-up is 5 s before the drop (the middle of the rise's last 10 s). An intro is hit when its end is within 3 s; on noise, when no intro is suggested. A false climax is a suggested climax more than 5 s from every truth.
- **CPU:** `std::clock` over 20 calls per clip, microseconds per `SuggestPhases` call. Linux 6.18, GCC 13, 4 cores shared with a parallel build (loadavg 4.8). The table is the first of three runs; the "all" rows of the other two were 32 / 34 and 38 / 40 us (before / arc).

## Five fixed cases (`tests/core/intensity_arc_test.cpp`)

Error of the climax stretch's midpoint and of the intro end, in seconds (+ late, - early). The "before" values were measured with the old rules on the same cases.

| Case | Truth | Climax before | Climax arc | Intro before | Intro arc |
|---|---|---|---|---|---|
| A climax at 30 % (37.5 s) | intro 10 s | missed (search starts at half the clip) | -0.25 | +0.5 | -1.0 |
| B loud talky intro, climax 87.5 s | intro 20 s | -1.25 | 0.0 | 0.0 | +0.5 |
| C linear build-up, top 95 s | intro 15 s | -9.75 | 0.0 | +3.0 | 0.0 |
| D two climaxes, 46 s and 98 s | intro 10 s | only 98 s (-1.0) | -0.25 and -0.25 | +0.5 | -1.0 |
| E motion peak under even music, 77.5 s | intro 10 s | -1.25 | 0.0 | +0.5 | 0.0 |

## 175 generated clips, `-O2`

| Scenario | Rules | Climax hits | Median climax error (s) | False climaxes | Intro hits | Median intro error (s) | CPU us / call |
|---|---|---|---|---|---|---|---|
| early climax (20-35 %) | before | 0/25 | - | 0 | 25/25 | 0.78 | 41 |
| early climax (20-35 %) | arc | 25/25 | 0.27 | 0 | 25/25 | 0.27 | 48 |
| late climax (75-88 %) | before | 25/25 | 1.50 | 0 | 25/25 | 0.77 | 33 |
| late climax (75-88 %) | arc | 25/25 | 0.16 | 0 | 25/25 | 0.23 | 33 |
| two climaxes | before | 25/50 | 74.66 | 0 | 25/25 | 0.63 | 39 |
| two climaxes | arc | 50/50 | 0.23 | 0 | 25/25 | 0.28 | 40 |
| linear build-up | before | 0/25 | 20.75 | 25 | 0/25 | 21.37 | 34 |
| linear build-up | arc | 17/25 | 2.17 | 7 | 25/25 | 0.30 | 34 |
| loud talky intro | before | 25/25 | 1.33 | 0 | 25/25 | 0.79 | 38 |
| loud talky intro | arc | 25/25 | 0.17 | 0 | 25/25 | 0.29 | 40 |
| flat music, motion peak | before | 0/25 | - | 0 | 25/25 | 0.84 | 33 |
| flat music, motion peak | arc | 25/25 | 0.21 | 0 | 25/25 | 0.17 | 36 |
| noise, no arc | before | 0/0 | - | 0 | 25/25 | - | 36 |
| noise, no arc | arc | 0/0 | - | 0 | 25/25 | - | 38 |
| **all** | before | 75/175 | 1.75 | 25 | 150/175 | 0.81 | 36 |
| **all** | arc | 167/175 | 0.25 | 7 | 175/175 | 0.27 | 38 |

In the unoptimised build directory (empty `CMAKE_BUILD_TYPE`) the "all" rows read 377 / 376 us. On a 2 h clip (14 400 steps) a call takes 0.9-1.1 ms before and 1.2-1.4 ms with the arc (`-O2`, two runs, scratch check). The arc keeps about eight vectors of one double per step: under 1 MB for 2 h.

## Reading the table

- **Early climaxes** were never found before: the old search started at the middle of the clip.
- **The flat-music clips** fail before for a second reason: the old rules clipped motion at its 90th percentile, and a climax shorter than 10 % of the clip lies above it, so the climax looked like the action. The arc caps motion at 2 x that level instead.
- **Two climaxes:** the old rules report one; the arc reports the second at half the primary's prominence or more, 20 s or more away.
- **Build-ups** are where the arc still misses: 8 of 25, all 7-16 s early, on rises of 100-500 s whose top is barely above the 30 s before it. The intro of a build-up is now found (25/25, before 0/25: the old steady-run threshold was reached far into the rise).
- **Noise** gives no climax and no intro with either rules.

Run it again: `cmake --build build --target phase_bench && build/tests/phase_bench [--variants N]`.
