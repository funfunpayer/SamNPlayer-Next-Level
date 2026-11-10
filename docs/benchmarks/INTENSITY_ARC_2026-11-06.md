# Intensity arc: phase suggestions before and after, 6-8 Nov 2026

The phase rules (D-157) against the intensity arc (D-165) on synthetic clips whose intro and climaxes are known. **Synthetic data only**: no real clip was measured. Every threshold of the arc that is not a plain time scale was fitted on the train seeds of these families, so the numbers below say how the arc does on clips like the ones it was fitted to. Real clips are the test that counts. Suggestions are never applied without the user's confirmation.

## Method

- **Tool:** `tests/bench/phase_bench.cpp`, target `phase_bench`, built with the tests, not in ctest. It keeps the old rules as `LegacyRules`, the regression oracle: the code of `core/src/phase_rules.cpp` at `33a5723`, less the script tempo and `active_share`, which no caller ever set. Both rule sets see the same clips and are graded on their suggested phases.
- **Two parts:**
  - 24 hand-made scenarios, 25 clips each (seeded LCG).
  - 18 families of random arcs, 300 clips each, from a **train** and a **held-out** set of seeds. The action is piecewise linear through 3-8 knots varying +-15 %, jitter is +-5 to +-25 % (+-50 % in the two noisy families, +-35 % with the loud opening), and the loudness is divided by its loudest step, as the label editor does.
- **Families added in the third review** (same rules, train and held-out):
  - *peak, then a long black and silent end:* the peak before 45 % of the clip, black and silent from 50-60 % to the end;
  - *loud opening, later peak, jitter +-35 %:* 20-60 s at 2x the motion and 2.2x the loudness, the peak at 60-90 % of the clip;
  - *50-80 % black or still and silent between:* steady action on either side of one long empty stretch, black or lit;
  - *40-60 % black at the start or end;*
  - *scene cuts, no sound:* a calm intro, steady motion, and on 2 % of the samples one step of 3-20x the motion.
- **Family added in the fourth review:** *50-70 % black with codec noise, silent or music.* Steady action either side of one long black stretch (luma 0.02). In it the motion is 0.002 +-50 %, as a codec's noise on black frames gives it, and the sound is silent or music at 0.25 +-20 %, half the clips each.
- **Seeds, and how often the held-out set was looked at.** The constants were chosen on the train seeds. The held-out seeds were evaluated once at the end of the second review. In the third review they were looked at more than once, so **the held-out numbers below are no longer from untouched data**:
  1. With the code frozen, they failed one row: *scene cuts, no sound*, 0.023 false climaxes per clip against a bound of 0.020. Four of its seven false climaxes sat on a cut in the clip's last two steps, which the running median did not cover yet. The train seeds had one such case. The median was extended to the clip's ends, with a unit test for that edge, and both seed sets were run again: no row worse.
  2. A later attempt to fix the quantised random walk (see Limits) passed the train seeds but made the held-out *loud opening* row fail (0.023 against 0.020). That attempt was reverted. The state of the third review is the one of step 1, and re-running it gave the same numbers.
  3. In the fourth review, the fix for codec noise on black (long empty stretches reach the arc without motion or sound) left every earlier row identical on both seed sets. The new family was run on both: 0 false climaxes per clip, against 0.873 / 0.840 before the fix.
- **Hits:**
  - A climax is hit when a suggested climax stretch has its midpoint within 5 s of the truth.
  - In the families it may also lie anywhere on a wider flat top: within max(5 s, the top's half-width) of its centre. Both rule sets are graded the same way; the strict 5 s numbers are below too.
  - Two bursts closer than the 20 s between climaxes count as one climax, hit at either burst.
  - A false climax is a suggestion that is no hit.
  - An intro is hit when its end is within 3 s, or, where the truth has none, when none is suggested.
- **Acceptance (exit 1 otherwise), per scenario and per family, train and held-out:**
  - at least as many climax hits as the old rules;
  - intro hits at most 2 % of the clips fewer;
  - false climaxes per clip at most the old rules' + 0.02. Where the old rules themselves exceed 0.25 per clip, the bound is old + 0.10: there both rule sets are unreliable.
- **CPU:** `std::clock` over 20 calls per clip, microseconds per `SuggestPhases` call, `-O2`, GCC 13, Linux 6.18, 4 shared cores. Another job shared the machine: the load average was 3.5-3.9. The old rules' times are about 4 % above the second review's, so the machine was a little slower.

## Five fixed cases (`tests/core/intensity_arc_test.cpp`)

Error of the climax stretch's midpoint and of the intro end, in seconds (+ late, - early).

| Case | Truth | Climax before | Climax arc | Intro before | Intro arc |
|---|---|---|---|---|---|
| A climax at 30 % (37.5 s) | intro 10 s | missed | +0.25 | +0.5 | -1.0 |
| B loud talky intro, climax 87.5 s | intro 20 s | -1.25 | +0.25 | 0.0 | +1.0 |
| C linear build-up, top 95 s | intro 15 s | -9.75 | -3.0 | +3.0 | +0.5 |
| D two climaxes, 46 s and 98 s | intro 10 s | only 98 s (-1.0) | 0.0 and -0.25 | +0.5 | -1.0 |
| E motion peak under even music, 77.5 s | intro 10 s | -1.25 | +0.25 | +0.5 | -1.0 |

## 24 scenarios (`-O2`)

| Scenario | Rules | Climax hits | Median climax error (s) | False climaxes | Intro hits | Median intro error (s) | CPU us / call |
|---|---|---|---|---|---|---|---|
| early climax (20-35 %) | before | 0/25 | - | 0 | 25/25 | 0.78 | 34 |
| early climax (20-35 %) | arc | 25/25 | 0.51 | 0 | 25/25 | 0.43 | 144 |
| late climax (75-88 %) | before | 25/25 | 1.50 | 0 | 25/25 | 0.77 | 29 |
| late climax (75-88 %) | arc | 25/25 | 0.31 | 0 | 25/25 | 0.27 | 108 |
| two climaxes | before | 25/50 | 74.66 | 0 | 25/25 | 0.63 | 31 |
| two climaxes | arc | 50/50 | 0.40 | 0 | 25/25 | 0.34 | 126 |
| linear build-up | before | 0/25 | 20.75 | 25 | 0/25 | 21.37 | 35 |
| linear build-up | arc | 15/25 | 4.34 | 10 | 25/25 | 0.33 | 122 |
| loud talky intro | before | 25/25 | 1.33 | 0 | 25/25 | 0.79 | 53 |
| loud talky intro | arc | 25/25 | 0.26 | 0 | 25/25 | 0.34 | 214 |
| flat music, motion peak | before | 0/25 | - | 0 | 25/25 | 0.84 | 49 |
| flat music, motion peak | arc | 25/25 | 0.29 | 0 | 25/25 | 0.24 | 202 |
| noise, no arc | before | 0/0 | - | 0 | 25/25 | - | 30 |
| noise, no arc | arc | 0/0 | - | 0 | 25/25 | - | 167 |
| climax runs to the end | before | 25/25 | 0.35 | 0 | 25/25 | 0.76 | 31 |
| climax runs to the end | arc | 25/25 | 0.98 | 0 | 25/25 | 0.28 | 127 |
| opens at its top (no climax) | before | 0/0 | - | 0 | 25/25 | - | 29 |
| opens at its top (no climax) | arc | 0/0 | - | 0 | 25/25 | - | 107 |
| plateau of 60-120 s | before | 1/25 | 11.37 | 16 | 25/25 | 0.50 | 30 |
| plateau of 60-120 s | arc | 20/25 | 0.24 | 0 | 25/25 | 0.49 | 117 |
| 30 s clip | before | 25/25 | 1.52 | 0 | 17/25 | 0.61 | 4 |
| 30 s clip | arc | 25/25 | 0.20 | 0 | 25/25 | 0.27 | 9 |
| 45 s clip | before | 25/25 | 0.80 | 0 | 25/25 | 0.88 | 5 |
| 45 s clip | arc | 25/25 | 0.50 | 0 | 25/25 | 0.26 | 12 |
| 60 s clip | before | 25/25 | 0.65 | 0 | 25/25 | 0.86 | 6 |
| 60 s clip | arc | 25/25 | 0.40 | 0 | 25/25 | 0.26 | 14 |
| 90 s clip | before | 25/25 | 0.47 | 0 | 25/25 | 0.52 | 9 |
| 90 s clip | arc | 25/25 | 0.31 | 0 | 25/25 | 0.28 | 21 |
| two bursts 10 s apart | before | 25/25 | 2.55 | 0 | 25/25 | 0.79 | 33 |
| two bursts 10 s apart | arc | 25/25 | 0.40 | 0 | 25/25 | 0.23 | 127 |
| two bursts 15 s apart | before | 9/25 | 5.10 | 9 | 25/25 | 0.65 | 32 |
| two bursts 15 s apart | arc | 25/25 | 0.16 | 0 | 25/25 | 0.41 | 120 |
| two bursts 20 s apart | before | 23/50 | 19.53 | 0 | 25/25 | 0.65 | 30 |
| two bursts 20 s apart | arc | 25/50 | 19.40 | 0 | 25/25 | 0.35 | 111 |
| two bursts 25 s apart | before | 23/50 | 24.20 | 0 | 25/25 | 0.81 | 31 |
| two bursts 25 s apart | arc | 50/50 | 0.31 | 0 | 25/25 | 0.23 | 130 |
| black cut inside the climax | before | 24/25 | 2.98 | 1 | 25/25 | 0.78 | 33 |
| black cut inside the climax | arc | 25/25 | 3.06 | 0 | 25/25 | 0.25 | 128 |
| slow drift, no climax | before | 0/0 | - | 0 | 25/25 | - | 27 |
| slow drift, no climax | arc | 0/0 | - | 0 | 25/25 | - | 102 |
| loud concert opening | before | 25/25 | 0.54 | 0 | 25/25 | - | 28 |
| loud concert opening | arc | 25/25 | 0.22 | 0 | 25/25 | - | 117 |
| starts mid-action | before | 21/25 | 0.23 | 0 | 25/25 | - | 31 |
| starts mid-action | arc | 25/25 | 0.44 | 0 | 25/25 | - | 133 |
| climax in the sound only | before | 18/25 | 1.97 | 1 | 25/25 | 0.76 | 32 |
| climax in the sound only | arc | 25/25 | 0.25 | 0 | 25/25 | 0.27 | 122 |
| climax in the motion only, no sound | before | 0/25 | 16.55 | 2 | 25/25 | 0.70 | 19 |
| climax in the motion only, no sound | arc | 25/25 | 0.31 | 0 | 25/25 | 0.30 | 65 |
| **all** | before | 369/600 | 1.43 | 54 | 567/600 | 0.75 | 28 |
| **all** | arc | 560/600 | 0.38 | 10 | 600/600 | 0.30 | 110 |

## Random arcs, train seeds

| Family | Climax hits before | Climax hits arc | False climaxes / clip before | False climaxes / clip arc | Intro hits before | Intro hits arc |
|---|---|---|---|---|---|---|
| peak in motion and sound | 54 % | 99 % | 0.003 | 0.000 | 300/300 | 298/300 |
| peak in the motion only | 14 % | 90 % | 0.003 | 0.000 | 297/300 | 299/300 |
| peak in the sound only | 48 % | 89 % | 0.003 | 0.000 | 298/300 | 300/300 |
| staircase up to a top | 66 % | 73 % | 0.133 | 0.010 | 258/300 | 300/300 |
| peak in motion and sound, jitter +-50 % | 57 % | 98 % | 0.000 | 0.017 | 300/300 | 296/300 |
| peak, then a long black and silent end | 52 % | 100 % | 0.000 | 0.000 | 300/300 | 300/300 |
| loud opening, later peak, jitter +-35 % | 94 % | 98 % | 0.000 | 0.013 | 300/300 | 300/300 |
| no peak: calm intro, even action | - | - | 0.000 | 0.000 | 298/300 | 300/300 |
| no peak: even action, calm end | - | - | 0.000 | 0.003 | 300/300 | 300/300 |
| no peak: random walk, step 0.005 | - | - | 0.000 | 0.000 | 298/300 | 300/300 |
| no peak: random walk, step 0.01 | - | - | 0.063 | 0.000 | 297/300 | 299/300 |
| no peak: random walk, step 0.02 | - | - | 0.337 | 0.173 | 299/300 | 298/300 |
| no peak: random walk, step 0.01, jitter +-50 % | - | - | 0.150 | 0.040 | 299/300 | 299/300 |
| no peak: 1 Hz beats in the sound | - | - | 0.000 | 0.000 | 298/300 | 300/300 |
| no peak: 50-80 % black or still and silent between | - | - | 0.003 | 0.000 | 300/300 | 300/300 |
| no peak: 40-60 % black at the start or end | - | - | 0.000 | 0.000 | 300/300 | 300/300 |
| no peak: scene cuts, no sound | - | - | 0.000 | 0.013 | 294/300 | 300/300 |
| no peak: 50-70 % black with codec noise, silent or music | - | - | 0.000 | 0.000 | 300/300 | 300/300 |

## Random arcs, held-out seeds (not untouched, see Method)

| Family | Climax hits before | Climax hits arc | False climaxes / clip before | False climaxes / clip arc | Intro hits before | Intro hits arc |
|---|---|---|---|---|---|---|
| peak in motion and sound | 58 % | 100 % | 0.000 | 0.000 | 297/300 | 298/300 |
| peak in the motion only | 16 % | 90 % | 0.003 | 0.010 | 296/300 | 290/300 |
| peak in the sound only | 51 % | 88 % | 0.000 | 0.000 | 297/300 | 297/300 |
| staircase up to a top | 64 % | 70 % | 0.127 | 0.007 | 270/300 | 300/300 |
| peak in motion and sound, jitter +-50 % | 59 % | 98 % | 0.000 | 0.013 | 300/300 | 299/300 |
| peak, then a long black and silent end | 53 % | 99 % | 0.000 | 0.000 | 300/300 | 300/300 |
| loud opening, later peak, jitter +-35 % | 95 % | 97 % | 0.000 | 0.017 | 300/300 | 300/300 |
| no peak: calm intro, even action | - | - | 0.000 | 0.000 | 295/300 | 300/300 |
| no peak: even action, calm end | - | - | 0.000 | 0.007 | 300/300 | 300/300 |
| no peak: random walk, step 0.005 | - | - | 0.003 | 0.000 | 297/300 | 300/300 |
| no peak: random walk, step 0.01 | - | - | 0.093 | 0.010 | 298/300 | 300/300 |
| no peak: random walk, step 0.02 | - | - | 0.320 | 0.173 | 291/300 | 298/300 |
| no peak: random walk, step 0.01, jitter +-50 % | - | - | 0.130 | 0.023 | 300/300 | 300/300 |
| no peak: 1 Hz beats in the sound | - | - | 0.000 | 0.000 | 300/300 | 300/300 |
| no peak: 50-80 % black or still and silent between | - | - | 0.000 | 0.000 | 300/300 | 300/300 |
| no peak: 40-60 % black at the start or end | - | - | 0.000 | 0.000 | 300/300 | 300/300 |
| no peak: scene cuts, no sound | - | - | 0.000 | 0.010 | 295/300 | 300/300 |
| no peak: 50-70 % black with codec noise, silent or music | - | - | 0.000 | 0.000 | 300/300 | 300/300 |

No family or scenario is worse than the old rules under the acceptance above, on either seed set. The margins are small:
- The held-out motion-only peak has 290 intro hits against 296, exactly the 2 % allowed.
- The loud opening and the scene cuts are 1-3 clips in 300 below their bound of 0.020 false climaxes per clip.

## What the fourth review's fix changed

Black frames are not exactly still: the app's motion is the mean absolute difference of 64x36 grey frames, so a codec's noise on black gives a small motion that is not 0. Such samples were empty for the phase rules (dark) but not for the arc. Now an empty stretch of at least 10 s (`kEmptyRunMs`) reaches the arc without motion or sound; shorter ones (a black cut inside a climax) keep theirs, since zeroing those splits the climax (the scenario's hits fell from 25 to 21 when every empty sample was zeroed). False climaxes per clip:

| Probe (600 s of steady action, the middle black with motion 0.002 +-50 %) | Old rules | `664744c` | Now |
|---|---|---|---|
| 50 % / 70 % black, silent (the review's probe) | 0 | 0.873 / 1.043 | 0 |
| 50 % / 70 % black, music at 0.25 (the review's probe) | 0 | 0.800 / 0.993 | 0 |
| the bench family, train / held-out | 0 / 0 | 0.873 / 0.840 | 0 / 0 |

Every other bench row is identical before and after.

## What the third review's fixes changed

On the review's own probes (`rv-arc4`, the review's generators and seeds), false climaxes per clip unless stated:

| Probe | Old rules | 6e4d314 | Now |
|---|---|---|---|
| 600 s of steady action, 50 / 60 / 80 % black and silent in the middle | 0 | 1.39 / 1.37 / 1.11 | 0 |
| the same, lit but still and silent | 0 | 1.38 / 1.36 / 1.11 | 0 |
| black and silent tail of 50 / 60 % (100 clips) | 0 | 0.41 / 0.55 | 0 |
| black and silent first 60 %, then steady action | 0 | 0.857 | 0 |
| motion exactly 0 except the action at 100-300 s | 0 | 1.053 | 0 |
| a real peak at 150 s, black tail from 300 s: climaxes per clip | 0 (missed) | 1.20 | 1.00, on the peak |
| 3 h stream, peak at 45 min, then 2 h black: climaxes per clip | 1.00 | 1.20 | 1.00 |
| no sound, calm intro, scene cuts on 0.4 % / 2 % of the steps | 0 / 0 | 0.067 / 0.423 | 0 / 0.020 |
| loud concert opening + later peak | 0 | 0.353 | 0.013 |
| the same, jitter +-35 % | 0 | 0.460 | 0.017 |
| lit, still and quiet (not silent) first 60 %, then action | 0 | 0.887 | 0.530 |
| loudness-only random walk, step 0.01 | 0.613 | 0.180 | 0.173 |
| **quantised random walk** (motion in tenths, loudness in sixteenths) | 0.273 | 0.143 | **0.443** |

On the bench (train seeds, wide grading), the rows that were there in the second review:

| Row | 6e4d314 | Now |
|---|---|---|
| peak in motion and sound: hits | 100 % | 99 % |
| peak in the motion only: hits | 91 % | 90 % |
| peak in the sound only: hits | 90 % | 89 % |
| staircase up to a top: hits / false per clip | 79 % / 0.003 | 73 % / 0.010 |
| jitter +-50 %: hits / false per clip | 99 % / 0.010 | 98 % / 0.017 |
| random walk, step 0.02: false per clip | 0.133 | 0.173 |
| random walk, step 0.01, jitter +-50 %: false per clip | 0.020 | 0.040 |
| 24 scenarios: hits / false | 562 / 8 | 560 / 10 (the linear build-up: 17 -> 15) |
| CPU, 24 scenarios, us per call | 99 (old rules 27) | 110 (old rules 28) |

The empty-stretch, scene-cut and loud-opening fixes cost a little everywhere else. The staircase and the scenarios lost to the running median. With the median switched off, the staircase is back at 79 % and the scenarios at 562 / 8, but scene cuts without sound give 0.663 false climaxes per clip. The quantised walk got worse (see Limits).

## The same families, strict 5 s hits

With the strict rule, a climax placed inside a wide top but more than 5 s off its centre counts as a miss and a false climax. The no-peak rows have no truth to miss and read as above. Under the strict rule the bench fails on three rows, on both seed sets:

- **The +-50 % mixed peak:** 83 % / 86 % hits against 57 % / 59 %, but 0.170 / 0.137 false climaxes per clip against 0. The misses lie on 20-30 s tops that the jitter splits into two humps.
- **The sound-only peak:** 0.027 / 0.040 false climaxes per clip against 0.003 / 0.007.
- **The loud opening:** 0.037 / 0.047 false climaxes per clip against 0, and on the held-out seeds 94 % hits against the old rules' 95 %.

| Family | Train hits before | Train hits arc | Train false / clip before | Train false / clip arc | Held-out hits before | Held-out hits arc | Held-out false / clip before | Held-out false / clip arc |
|---|---|---|---|---|---|---|---|---|
| peak in motion and sound | 53 % | 99 % | 0.007 | 0.007 | 57 % | 100 % | 0.007 | 0.000 |
| peak in the motion only | 13 % | 89 % | 0.007 | 0.017 | 15 % | 88 % | 0.007 | 0.027 |
| peak in the sound only | 48 % | 87 % | 0.003 | 0.027 | 50 % | 84 % | 0.007 | 0.040 |
| staircase up to a top | 38 % | 68 % | 0.413 | 0.057 | 36 % | 65 % | 0.403 | 0.057 |
| peak in motion and sound, jitter +-50 % | 57 % | 83 % | 0.000 | 0.170 | 59 % | 86 % | 0.000 | 0.137 |
| peak, then a long black and silent end | 51 % | 100 % | 0.003 | 0.000 | 53 % | 99 % | 0.000 | 0.000 |
| loud opening, later peak, jitter +-35 % | 94 % | 95 % | 0.000 | 0.037 | 95 % | 94 % | 0.000 | 0.047 |

## Sensitivity of the fitted constants (train seeds, one at a time)

The columns are:
- Peak hits: mixed / motion only / sound only / staircase / +-50 % jitter / loud opening.
- False climaxes per clip: walk 0.02 / noisy walk / scene cuts without sound / empty middle.

| Constant | Value | Peak hits | False per clip | Scenario hits / false | Rows worse than the old rules |
|---|---|---|---|---|---|
| all | as chosen | 99 / 90 / 89 / 73 / 98 / 98 % | 0.173 / 0.040 / 0.013 / 0.000 | 560/600 / 10 | 0 |
| `kFlankGain` | 1.28 (x0.8) | 99 / 98 / 99 / 83 / 98 / 98 % | 0.570 / 0.377 / 0.013 / 0.000 | 560/600 / 10 | 2 (both walks) |
| `kFlankGain` | 1.45 | 99 / 95 / 97 / 77 / 98 / 98 % | 0.273 / 0.077 / 0.013 / 0.000 | 560/600 / 10 | 0 |
| `kFlankGain` | 1.8 | 99 / 79 / 75 / 72 / 97 / 98 % | 0.113 / 0.037 / 0.013 / 0.000 | 560/600 / 10 | 0 |
| `kFlankGain` | 1.92 (x1.2) | 97 / 77 / 71 / 71 / 95 / 97 % | 0.097 / 0.033 / 0.013 / 0.000 | 560/600 / 10 | 0 |
| `kLastStepGain` | 1.1 | 99 / 90 / 89 / 74 / 98 / 98 % | 0.173 / 0.040 / 0.013 / 0.000 | 560/600 / 10 | 0 |
| `kLastStepGain` | 1.25 | 99 / 90 / 89 / 41 / 98 / 98 % | 0.170 / 0.040 / 0.013 / 0.000 | 560/600 / 10 | 1 (staircase) |
| `kLastStepGain` | 1.38 (x1.2) | 99 / 90 / 89 / 5 / 98 / 98 % | 0.167 / 0.040 / 0.013 / 0.000 | 560/600 / 10 | 1 (staircase) |
| `kSuddenMs` | 2000 | 99 / 78 / 81 / 41 / 98 / 98 % | 0.133 / 0.037 / 0.013 / 0.000 | 560/600 / 10 | 1 (staircase) |
| `kSuddenMs` | 4000 | 99 / 93 / 94 / 74 / 98 / 98 % | 0.210 / 0.047 / 0.013 / 0.000 | 560/600 / 10 | 0 |
| `kSuddenNoise` | 4.0 | 99 / 91 / 90 / 75 / 98 / 98 % | 0.243 / 0.230 / 0.013 / 0.000 | 560/600 / 10 | 1 (noisy walk) |
| `kSuddenNoise` | 8.0 | 99 / 89 / 86 / 69 / 98 / 98 % | 0.123 / 0.010 / 0.010 / 0.000 | 560/600 / 10 | 0 |
| `kCalmSpread` | 0.75 | 99 / 90 / 89 / 73 / 98 / 98 % | 0.160 / 0.040 / 0.013 / 0.000 | 547/600 / 10 | 0 |
| `kCalmSpread` | 1.5 | 99 / 90 / 89 / 73 / 98 / 98 % | 0.177 / 0.047 / 0.013 / 0.000 | 560/600 / 10 | 0 |
| `kIntroCalm` | 0.4 and 0.6 | as chosen | as chosen | 560/600 / 10 | 0 |
| `kActionLevel` | 0.55 and 0.75 | as chosen | as chosen | 560/600 / 10 | 0 |
| `kCutMedian` | 3 | 99 / 90 / 88 / 72 / 98 / 98 % | 0.170 / 0.033 / 0.253 / 0.000 | 561/600 / 9 | 1 (scene cuts) |
| `kCutMedian` | 7 | 99 / 90 / 89 / 70 / 98 / 98 % | 0.170 / 0.053 / 0.007 / 0.000 | 557/600 / 13 | 0 |

- **`kFlankGain` is a knife edge.** At x0.8 the walks' false climaxes rise 3-9x (0.173 -> 0.570, 0.040 -> 0.377) and the bench fails. At x1.2 the motion-only and sound-only peaks lose 13-18 points. On the review's own families the swing was larger: x1.2 lost 20-45 points of peak hits, and x0.8 raised the false climaxes of walks and no-sound clips 3-20x.
- **`kLastStepGain` decides the staircase.** At x1.2 (1.38) the staircase family drops from 73 % to 5 %; the review measured 53 % -> 21 % on its staircases.
- **`kSuddenMs`:** 2 s loses the staircase.
- **`kSuddenNoise`:** 4 lets +-50 % jitter pass as a sudden edge.
- **`kCutMedian`:** 3 steps does not take out cuts that fall close together (0.253 per clip); 7 costs scenario hits.
- **Flat in this range:** `kIntroCalm`, `kActionLevel`, and `kCalmSpread`, except that 0.75 loses 13 scenario hits (second climaxes).
- **Not swept:** `kFullStrength`, `kMaxRatio`, `kMinProminence`.

## Other probes

The review's generators (`rv-arc4` probes and families, 300 clips per family, its default seed) at this state:

- **Empty stretches:** every black or still-and-silent middle of 30-80 %, black tails and black lead-ins give 0 false climaxes (old 0). Five-second black gaps every 30 s: 0.
- **Next to black:** a peak beside a black tail is found in 100 of 100 clips (old 0). A 3 h stream with 2 h of black gets 1.0 climax per clip, the right one.
- **One input:**
  - Sound-only steady action: 0 false per clip; sound-only random walk: 0.173 (old 0 and 0.613).
  - Motion-only (loudness exactly 0) steady action: 0; with a 1.8x peak: 300 of 300 hits (old 58).
- **Quantised input** (motion in tenths, loudness in sixteenths):
  - steady action 0, a one-quantum step 0;
  - a 10 s bump of one quantum: 300 of 300 hits (old 0);
  - a quantised random walk: 0.443 false per clip (old 0.273), see Limits.
- **Peaks:** two equal peaks 25-60 s apart: 99-100 % hits (old 26-29 %). Ramp and spike: 100 % (old 95 %).
- **Beats** at 0.5, 1 and 2 Hz: at most 0.003 false per clip.
- **Loud opening with jitter +-35 %:** 98.3 % strict and 99.7 % wide hits, against the old rules' 100 % and 100 %.
- **The review's fuzzer under ASan and UBSan.** Its inputs:
  - random and hostile values: NaN, infinities, negatives, 1e308, denormals, mismatched lengths;
  - `step_ms` of 0, -500, 1, 33, 100 000, 2^40, 2^44 and INT64_MIN;
  - option values out of range;
  - 3 h and 10 h clips.

  At this state: 13 300 cases, 136 of them 3 h or 10 h clips. No sanitizer report, and no invariant broken. The invariants:
  - every peak's top inside its stretch, every confidence in 0..1;
  - suggested phases non-empty, in order and not overlapping, unconfirmed, source "rules";
  - the same phases on a second call.

## Limits, plainly

- **Synthetic only, fitted on synthetic data.** Every fitted constant was chosen on the train seeds of these generators. Real clips may need other values; they are the gate, not this benchmark.
- **Staircases under jitter.** The arc finds the top step of a clean staircase (73 % / 70 % here, old 66 % / 64 %), but the review's staircases with jitter sd above about 0.2 lose: 19 % against 68 % strict at sd 0.27. The arc wins (79 % against 66 %) only at low jitter. On the review's families at this state: staircase to the end 52 % against 81 % (wide), up then down 52 % against 92 %.
- **Faster random walks are worse than the old rules.** The bench's walks move by at most about 0.006 (sd) per 500 ms step. The review's walks with steps of 0.01-0.04 gave the arc 1.07-1.63 false climaxes per clip against 0.79-0.90 at `6e4d314`. At this state:

  | Walk step | Arc | Old rules |
  |---|---|---|
  | 0.01 | 0.54-0.76 | 0.63-0.72 |
  | 0.02 | 1.12-1.21 | 0.77-0.84 |
  | 0.04 | 1.42-1.65 | 0.87-0.91 |

  The slow walks (0.003-0.005) are better than the old rules: 0.02-0.33 against 0.19-0.52. Neither rule set should be trusted on restless content.
- **Quantised random walks got worse in this round:** 0.443 false climaxes per clip, against 0.143 at `6e4d314` and 0.273 for the old rules.
  - Cause: the running median against scene cuts turns motion that flickers between two quanta into clean steps, and such a step passes as a sudden edge, which needs only `climax_gain`.
  - Three fixes were tried: the noise measured before the median; the sudden edge read before the median; and that edge required at three quarters at the top's edge. The best of them still left 0.317, above the old rules, and made a held-out row fail, so all three were reverted.
  - In the app it cannot occur: the motion is a mean of absolute 8-bit differences over 64x36 pixels divided by 255, so its quantum is about 1.7e-6, and the loudness is a float RMS in dB. The fourth review accepted the limit on this ground.
- **A climax in the sound alone needs a much larger rise than one in the picture.** The arc blends motion and loudness 0.6 / 0.4, so a rise of the loudness moves it less.
  - The review found that a sound-only climax needs about 1.63x the rest of the action: a sound-only 1.5x peak was found in 0 % of its clips, against 99 % for a motion-only one.
  - A probe here used a 12 s top, +-20 % jitter and 200 clips per row. Sound-only peaks were found in 0 % at 1.2-1.6x, 4-6 % at 1.7-1.8x and 30 % at 2x.
  - Motion-only peaks with even sound were found in 27 % at 1.4x and 78-98 % at 1.5-2x; without sound, 67 % at 1.3x.
  - The bench's sound-only family (1.75-3.8x) gets 89 %.
- **Calm tails:** 0.023-0.032 false climaxes per clip in the review's calm-tail family at `6e4d314` (old 0.001). At the third review's state it was 0.010 on the review's default seed against 0, but 0.023 on a fresh seed.
- **App-like loudness adds spurious intros on ramps and staircases.** Measured by the fourth review with the loudness passed through `NormalizeLoudness` (RMS, dB, a floor at the 20th percentile, a gate):
  - intro hits on ramp and spike fall from 994 to 891, and on a staircase up then down from 809 to 624;
  - `6e4d314` had the same (881 / 622);
  - cause: the quietest 20 % of a ramp maps to loudness 0 and looks like a calm intro;
  - peak hits stay far above the old rules under this loudness (1 h clips: 85 % against 31 %).
- **A short black cut inside a climax with its motion gone** (codec noise, the sound going on) splits the climax into two bursts. The suggested climax lies inside the true one, but it may be either burst: 90-98 s instead of 101-109 s in the test's clip, where the true climax is 92-110 s.
- **Strict grading:** on wide or ripply tops the arc often places the climax more than 5 s off the centre. In the review's plateau with ripple that gives 0.937 false climaxes per clip under strict grading, against 0.010 under wide grading.
- **A lit, still and quiet but not silent lead-in** (loudness 0.06, above the 0.05 silence threshold) over the first 60 % of the clip, then action: 0.53 false climaxes per clip (old 0). The lead-in is not empty, and it is longer than the 40 % the intro may take.
- **Slow build-ups** of 100-500 s: 15 of 25 in the scenario, all early.
- **Bursts 20 s apart** fuse into one climax (25 of 50 truths, as with the old rules).
- **A last staircase step below 1.15x the step before** is no climax.
- **A clip that opens at its top** gets no climax suggestion there: a conservative choice, since the arc cannot tell a cold open from a film cut inside its climax.
- **Confidence** ranks climaxes and is no probability.
- **CPU:** a call costs about 4x the old rules: 110 us against 28 us over the 24 scenarios. On a 2 h clip of 14 400 steps it is 4.1-4.4 ms against 0.9-1.3 ms (3.5 ms against 0.75-1.1 ms in the second review, on a less loaded machine). That is still small next to decoding the picture. Adversarial inputs cost more (the fourth review): up to 10 ms on a 2 h clip, and 102 ms on a 10 h train of spikes. Memory: about a dozen vectors of one double per step, around 1.4 MB for 2 h.

Run it again: `cmake --build build --target phase_bench && build/tests/phase_bench [--split train|held-out|both] [--strict-hits] [--family-clips N] [--variants N]`.
