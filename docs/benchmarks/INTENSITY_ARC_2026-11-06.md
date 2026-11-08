# Intensity arc: phase suggestions before and after, 6-8 Nov 2026

The phase rules (D-157) against the intensity arc (D-165) on synthetic clips whose intro and climaxes are known. **Synthetic data only**: no real clip was measured. Every threshold of the arc that is not a plain time scale was fitted on the train seeds of these families; the sensitivity grid below shows how much each one matters. Suggestions are never applied without the user's confirmation.

## Method

- **Tool:** `tests/bench/phase_bench.cpp`, target `phase_bench`, built with the tests, not in ctest. It keeps the old rules as `LegacyRules`, the regression oracle: the code of `core/src/phase_rules.cpp` at `33a5723`, less the script tempo and `active_share`, which no caller ever set. Both rule sets see the same clips and are graded on their suggested phases.
- **Two parts:**
  - 24 hand-made scenarios, 25 clips each (seeded LCG).
  - 12 families of random arcs, 300 clips each, from a **train** and a **held-out** set of seeds. The action is piecewise linear through 3-8 knots varying +-15 %, jitter is +-5 to +-25 % (+-50 % in the two noisy families), and the loudness is divided by its loudest step, as the label editor does. The constants were chosen on the train seeds; the held-out seeds were evaluated once, after the code was frozen.
- **Hits:**
  - A climax is hit when a suggested climax stretch has its midpoint within 5 s of the truth.
  - In the families it may also lie anywhere on a wider flat top: within max(5 s, the top's half-width) of its centre. This was added after the noisy family's misses turned out to sit inside 20-30 s tops. Both rule sets are graded the same way; the strict 5 s numbers are below too.
  - Two bursts closer than the 20 s between climaxes count as one climax, hit at either burst.
  - A false climax is a suggestion that is no hit.
  - An intro is hit when its end is within 3 s, or, where the truth has none, when none is suggested.
- **Acceptance (exit 1 otherwise), per scenario and per family, train and held-out:**
  - at least as many climax hits as the old rules;
  - intro hits at most 2 % of the clips fewer;
  - false climaxes per clip at most the old rules' + 0.02. Where the old rules themselves exceed 0.25 per clip (fast random walks, the "unusable regime"), the bound is old + 0.10: there both rule sets are unreliable.
- **CPU:** `std::clock` over 20 calls per clip, microseconds per `SuggestPhases` call, `-O2`, GCC 13, Linux 6.18, 4 shared cores (loadavg about 2.7).

## Five fixed cases (`tests/core/intensity_arc_test.cpp`)

Error of the climax stretch's midpoint and of the intro end, in seconds (+ late, - early).

| Case | Truth | Climax before | Climax arc | Intro before | Intro arc |
|---|---|---|---|---|---|
| A climax at 30 % (37.5 s) | intro 10 s | missed | +0.25 | +0.5 | -1.0 |
| B loud talky intro, climax 87.5 s | intro 20 s | -1.25 | +0.25 | 0.0 | +0.5 |
| C linear build-up, top 95 s | intro 15 s | -9.75 | -3.0 | +3.0 | +0.5 |
| D two climaxes, 46 s and 98 s | intro 10 s | only 98 s (-1.0) | 0.0 and -0.25 | +0.5 | -1.0 |
| E motion peak under even music, 77.5 s | intro 10 s | -1.25 | +0.25 | +0.5 | -0.5 |

## 24 scenarios (`-O2`)

| Scenario | Rules | Climax hits | Median climax error (s) | False climaxes | Intro hits | Median intro error (s) | CPU us / call |
|---|---|---|---|---|---|---|---|
| early climax (20-35 %) | before | 0/25 | - | 0 | 25/25 | 0.78 | 28 |
| early climax (20-35 %) | arc | 25/25 | 0.52 | 0 | 25/25 | 0.27 | 116 |
| late climax (75-88 %) | before | 25/25 | 1.50 | 0 | 25/25 | 0.77 | 22 |
| late climax (75-88 %) | arc | 25/25 | 0.31 | 0 | 25/25 | 0.23 | 82 |
| two climaxes | before | 25/50 | 74.66 | 0 | 25/25 | 0.63 | 24 |
| two climaxes | arc | 50/50 | 0.41 | 0 | 25/25 | 0.30 | 97 |
| linear build-up | before | 0/25 | 20.75 | 25 | 0/25 | 21.37 | 27 |
| linear build-up | arc | 17/25 | 3.88 | 8 | 25/25 | 0.30 | 87 |
| loud talky intro | before | 25/25 | 1.33 | 0 | 25/25 | 0.79 | 30 |
| loud talky intro | arc | 25/25 | 0.25 | 0 | 25/25 | 0.34 | 127 |
| flat music, motion peak | before | 0/25 | - | 0 | 25/25 | 0.84 | 26 |
| flat music, motion peak | arc | 25/25 | 0.29 | 0 | 25/25 | 0.17 | 114 |
| noise, no arc | before | 0/0 | - | 0 | 25/25 | - | 24 |
| noise, no arc | arc | 0/0 | - | 0 | 25/25 | - | 124 |
| climax runs to the end | before | 25/25 | 0.35 | 0 | 25/25 | 0.76 | 24 |
| climax runs to the end | arc | 25/25 | 0.98 | 0 | 25/25 | 0.24 | 105 |
| opens at its top (no climax) | before | 0/0 | - | 0 | 25/25 | - | 24 |
| opens at its top (no climax) | arc | 0/0 | - | 0 | 25/25 | - | 86 |
| plateau of 60-120 s | before | 1/25 | 11.37 | 16 | 25/25 | 0.50 | 35 |
| plateau of 60-120 s | arc | 20/25 | 0.24 | 0 | 25/25 | 0.39 | 121 |
| 30 s clip | before | 25/25 | 1.52 | 0 | 17/25 | 0.61 | 3 |
| 30 s clip | arc | 25/25 | 0.20 | 0 | 25/25 | 0.27 | 6 |
| 45 s clip | before | 25/25 | 0.80 | 0 | 25/25 | 0.88 | 4 |
| 45 s clip | arc | 25/25 | 0.54 | 0 | 25/25 | 0.29 | 7 |
| 60 s clip | before | 25/25 | 0.65 | 0 | 25/25 | 0.86 | 5 |
| 60 s clip | arc | 25/25 | 0.38 | 0 | 25/25 | 0.26 | 10 |
| 90 s clip | before | 25/25 | 0.47 | 0 | 25/25 | 0.52 | 7 |
| 90 s clip | arc | 25/25 | 0.32 | 0 | 25/25 | 0.28 | 14 |
| two bursts 10 s apart | before | 25/25 | 2.55 | 0 | 25/25 | 0.79 | 27 |
| two bursts 10 s apart | arc | 25/25 | 0.40 | 0 | 25/25 | 0.22 | 104 |
| two bursts 15 s apart | before | 9/25 | 5.10 | 9 | 25/25 | 0.65 | 39 |
| two bursts 15 s apart | arc | 25/25 | 0.19 | 0 | 25/25 | 0.35 | 131 |
| two bursts 20 s apart | before | 23/50 | 19.53 | 0 | 25/25 | 0.65 | 40 |
| two bursts 20 s apart | arc | 25/50 | 19.50 | 0 | 25/25 | 0.35 | 129 |
| two bursts 25 s apart | before | 23/50 | 24.20 | 0 | 25/25 | 0.81 | 41 |
| two bursts 25 s apart | arc | 50/50 | 0.24 | 0 | 25/25 | 0.19 | 150 |
| black cut inside the climax | before | 24/25 | 2.98 | 1 | 25/25 | 0.78 | 41 |
| black cut inside the climax | arc | 25/25 | 3.06 | 0 | 25/25 | 0.22 | 133 |
| slow drift, no climax | before | 0/0 | - | 0 | 25/25 | - | 36 |
| slow drift, no climax | arc | 0/0 | - | 0 | 25/25 | - | 121 |
| loud concert opening | before | 25/25 | 0.54 | 0 | 25/25 | - | 39 |
| loud concert opening | arc | 25/25 | 0.24 | 0 | 25/25 | - | 133 |
| starts mid-action | before | 21/25 | 0.23 | 0 | 25/25 | - | 42 |
| starts mid-action | arc | 25/25 | 0.44 | 0 | 25/25 | - | 159 |
| climax in the sound only | before | 18/25 | 1.97 | 1 | 25/25 | 0.76 | 41 |
| climax in the sound only | arc | 25/25 | 0.17 | 0 | 25/25 | 0.24 | 140 |
| climax in the motion only, no sound | before | 0/25 | 16.55 | 2 | 25/25 | 0.70 | 28 |
| climax in the motion only, no sound | arc | 25/25 | 0.32 | 0 | 25/25 | 0.30 | 78 |
| **all** | before | 369/600 | 1.43 | 54 | 567/600 | 0.75 | 27 |
| **all** | arc | 562/600 | 0.37 | 8 | 600/600 | 0.28 | 99 |

## Random arcs, train seeds

| Family | Climax hits before | Climax hits arc | False climaxes / clip before | False climaxes / clip arc | Intro hits before | Intro hits arc |
|---|---|---|---|---|---|---|
| peak in motion and sound | 54 % | 100 % | 0.003 | 0.000 | 300/300 | 298/300 |
| peak in the motion only | 14 % | 91 % | 0.003 | 0.000 | 297/300 | 299/300 |
| peak in the sound only | 48 % | 90 % | 0.003 | 0.000 | 298/300 | 300/300 |
| staircase up to a top | 66 % | 79 % | 0.133 | 0.003 | 258/300 | 300/300 |
| peak in motion and sound, jitter +-50 % | 57 % | 99 % | 0.000 | 0.010 | 300/300 | 295/300 |
| no peak: calm intro, even action | - | - | 0.000 | 0.000 | 297/300 | 300/300 |
| no peak: even action, calm end | - | - | 0.000 | 0.007 | 300/300 | 300/300 |
| no peak: random walk, step 0.005 | - | - | 0.007 | 0.000 | 296/300 | 299/300 |
| no peak: random walk, step 0.01 | - | - | 0.070 | 0.007 | 300/300 | 300/300 |
| no peak: random walk, step 0.02 | - | - | 0.297 | 0.133 | 294/300 | 298/300 |
| no peak: random walk, step 0.01, jitter +-50 % | - | - | 0.140 | 0.020 | 298/300 | 299/300 |
| no peak: 1 Hz beats in the sound | - | - | 0.000 | 0.000 | 299/300 | 300/300 |

## Random arcs, held-out seeds (evaluated once)

| Family | Climax hits before | Climax hits arc | False climaxes / clip before | False climaxes / clip arc | Intro hits before | Intro hits arc |
|---|---|---|---|---|---|---|
| peak in motion and sound | 58 % | 100 % | 0.000 | 0.000 | 297/300 | 297/300 |
| peak in the motion only | 16 % | 92 % | 0.003 | 0.010 | 296/300 | 290/300 |
| peak in the sound only | 51 % | 89 % | 0.000 | 0.000 | 297/300 | 297/300 |
| staircase up to a top | 64 % | 77 % | 0.127 | 0.013 | 270/300 | 300/300 |
| peak in motion and sound, jitter +-50 % | 59 % | 100 % | 0.000 | 0.000 | 300/300 | 298/300 |
| no peak: calm intro, even action | - | - | 0.000 | 0.000 | 298/300 | 300/300 |
| no peak: even action, calm end | - | - | 0.000 | 0.010 | 300/300 | 300/300 |
| no peak: random walk, step 0.005 | - | - | 0.000 | 0.000 | 298/300 | 300/300 |
| no peak: random walk, step 0.01 | - | - | 0.057 | 0.013 | 295/300 | 300/300 |
| no peak: random walk, step 0.02 | - | - | 0.390 | 0.217 | 299/300 | 296/300 |
| no peak: random walk, step 0.01, jitter +-50 % | - | - | 0.130 | 0.020 | 298/300 | 298/300 |
| no peak: 1 Hz beats in the sound | - | - | 0.000 | 0.000 | 298/300 | 300/300 |

No family or scenario is worse than the old rules under the acceptance above, on either seed set.

## The same families, strict 5 s hits

With the strict rule, a climax placed inside a wide top but more than 5 s off its centre counts as a miss and a false climax. Under it the bench fails on three rows:

- **The +-50 % mixed peak**, train and held-out: 88 % / 93 % hits against 57 % / 59 %, but 0.123 / 0.070 false climaxes per clip against 0. The misses lie on 20-30 s tops that the jitter splits into two humps.
- **The held-out sound-only peak:** 0.030 false climaxes per clip against 0.007.

Train:

| Family | Climax hits before | Climax hits arc | False / clip before | False / clip arc |
|---|---|---|---|---|
| peak in motion and sound | 53 % | 100 % | 0.007 | 0.000 |
| peak in the motion only | 13 % | 90 % | 0.007 | 0.007 |
| peak in the sound only | 48 % | 89 % | 0.003 | 0.010 |
| staircase up to a top | 38 % | 74 % | 0.413 | 0.057 |
| peak in motion and sound, jitter +-50 % | 57 % | 88 % | 0.000 | 0.123 |
| no peak: calm intro, even action | - | - | 0.000 | 0.000 |
| no peak: even action, calm end | - | - | 0.000 | 0.007 |
| no peak: random walk, step 0.005 | - | - | 0.007 | 0.000 |
| no peak: random walk, step 0.01 | - | - | 0.070 | 0.007 |
| no peak: random walk, step 0.02 | - | - | 0.297 | 0.133 |
| no peak: random walk, step 0.01, jitter +-50 % | - | - | 0.140 | 0.020 |
| no peak: 1 Hz beats in the sound | - | - | 0.000 | 0.000 |

Held-out:

| Family | Climax hits before | Climax hits arc | False / clip before | False / clip arc |
|---|---|---|---|---|
| peak in motion and sound | 57 % | 100 % | 0.007 | 0.000 |
| peak in the motion only | 15 % | 90 % | 0.007 | 0.023 |
| peak in the sound only | 50 % | 86 % | 0.007 | 0.030 |
| staircase up to a top | 36 % | 72 % | 0.403 | 0.060 |
| peak in motion and sound, jitter +-50 % | 59 % | 93 % | 0.000 | 0.070 |
| no peak: calm intro, even action | - | - | 0.000 | 0.000 |
| no peak: even action, calm end | - | - | 0.000 | 0.010 |
| no peak: random walk, step 0.005 | - | - | 0.000 | 0.000 |
| no peak: random walk, step 0.01 | - | - | 0.057 | 0.013 |
| no peak: random walk, step 0.02 | - | - | 0.390 | 0.217 |
| no peak: random walk, step 0.01, jitter +-50 % | - | - | 0.130 | 0.020 |
| no peak: 1 Hz beats in the sound | - | - | 0.000 | 0.000 |

## Sensitivity of the fitted constants (train seeds, one at a time)

| Constant | Value | Peak hits (mixed / motion / sound / staircase / noisy) | False per clip (walk 0.02 / noisy walk) | Scenario hits / false | Families or scenarios worse |
|---|---|---|---|---|---|
| `kFlankGain` | 1.6 | 100% / 91% / 90% / 79% / 99% | 0.133 / 0.020 | 562/600 / 8 | 0 |
| `kFlankGain` | 1.45 | 100% / 95% / 98% / 83% / 99% | 0.247 / 0.050 | 562/600 / 8 | 0 |
| `kFlankGain` | 1.8 | 99% / 79% / 76% / 78% / 98% | 0.073 / 0.020 | 562/600 / 8 | 0 |
| `kSuddenMs` | 2000 | 100% / 81% / 82% / 45% / 99% | 0.110 / 0.010 | 562/600 / 8 | 1 |
| `kSuddenMs` | 4000 | 100% / 94% / 94% / 82% / 99% | 0.157 / 0.023 | 562/600 / 8 | 0 |
| `kSuddenNoise` | 4.0 | 100% / 91% / 90% / 80% / 99% | 0.197 / 0.177 | 562/600 / 8 | 1 |
| `kSuddenNoise` | 8.0 | 100% / 91% / 90% / 75% / 99% | 0.123 / 0.003 | 562/600 / 8 | 0 |
| `kCalmSpread` | 0.75 | 100% / 91% / 90% / 79% / 99% | 0.133 / 0.020 | 547/600 / 8 | 0 |
| `kCalmSpread` | 1.5 | 100% / 91% / 90% / 79% / 99% | 0.143 / 0.020 | 562/600 / 8 | 0 |
| `kLastStepGain` | 1.1 | 100% / 91% / 90% / 80% / 99% | 0.133 / 0.020 | 562/600 / 8 | 0 |
| `kLastStepGain` | 1.25 | 100% / 91% / 90% / 44% / 99% | 0.133 / 0.020 | 562/600 / 8 | 1 |
| `kIntroCalm` | 0.4 | 100% / 91% / 90% / 79% / 99% | 0.133 / 0.020 | 562/600 / 8 | 0 |
| `kIntroCalm` | 0.6 | 100% / 91% / 90% / 79% / 99% | 0.133 / 0.020 | 562/600 / 8 | 0 |
| `kActionLevel` | 0.55 | 100% / 91% / 90% / 79% / 99% | 0.133 / 0.020 | 562/600 / 8 | 0 |
| `kActionLevel` | 0.75 | 100% / 91% / 90% / 79% / 99% | 0.133 / 0.020 | 562/600 / 8 | 1 |

The chosen values are the first row's.
- **Most sensitive:**
  - `kFlankGain`: lower finds more motion-only and sound-only peaks but doubles the walk's false climaxes; higher loses peaks.
  - `kSuddenMs`: 2 s loses the staircase.
  - `kSuddenNoise`: 4 lets +-50 % jitter pass as sudden.
  - `kLastStepGain`: 1.25 loses the staircase.
- **Flat in this range:** `kCalmSpread`, `kIntroCalm`, `kActionLevel`, except that 0.75 for `kCalmSpread` loses 15 scenario hits (second climaxes) and 0.75 for `kActionLevel` breaks one row.
- **Not swept:** `kFullStrength`, `kMaxRatio` and `kMinProminence`.

## Other probes

The review's own generators (`rv-arc3` probes), at the final code:
- Calm intro + even action, 300 clips: 0 false climaxes (old 0).
- No-peak arcs with action varying +-0 to +-30 %: at most 0.002 per clip (old up to 0.032).
- Climaxes carried by the sound alone (loudness 0.3 to 0.6-1.0, motion even or absent): 79-100 % hits (old 26-90.5 %).
- 5000 random peaked arcs: 99.9 % hits (old 58.3 %); 5000 no-peak arcs: 0.000 false per clip (old 0.004).
- Random walks at three step sizes and three jitter levels: at or below the old rules in every band. Step 0.02 with +-50 % jitter: 0.365 / 0.380 vs 0.665 / 0.605.
- The review's fuzzer (100 000 random inputs plus 3 h and 10 h clips) runs clean under ASan and UBSan.

## Limits, plainly

- **Synthetic only.** Real clips may need other thresholds; the fitted constants are listed above with their sensitivity.
- **Fast random walks** (step 0.02, the unusable regime): 0.13-0.22 false climaxes per clip for the arc (old 0.30-0.39). Neither rule set is reliable on such restless content.
- **Slow build-ups** of 100-500 s: 17 of 25 in the scenario, all early.
- **Bursts 20 s apart** fuse into one climax (25 of 50 truths, as with the old rules).
- **A last staircase step below 1.15x the step before** is no climax. In the staircase family the arc finds 79 % / 77 % (old 66 % / 64 %).
- **A clip that opens at its top** gets no climax suggestion there: a conservative choice, since the arc cannot tell a cold open from a film cut inside its climax.
- **Confidence** ranks climaxes and is no probability.
- **CPU:** a call costs about 3.5-4x the old rules: 84-99 us vs 22-27 us here, and 3.5 ms vs 0.75-1.1 ms on a 2 h clip of 14 400 steps. That is still small next to decoding the picture. Memory: about a dozen vectors of one double per step, around 1.4 MB for 2 h.

Run it again: `cmake --build build --target phase_bench && build/tests/phase_bench [--split train|held-out|both] [--strict-hits] [--family-clips N] [--variants N]`.
