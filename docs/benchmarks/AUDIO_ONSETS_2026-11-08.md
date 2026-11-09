# Audio onsets, tempo, beats and the heart rhythm: before and after (8 Nov 2026, revised 9 Nov after review)

Decision: D-168. Code: `core/include/samn/audio_onsets.h`, `audio_tempo.h`, `base_rhythm.h`, `improve.h` (`FillGapsRhythmic`).
Bench: `tests/bench/audio_onset_bench.cpp` (built with the tests; `audio_onset_bench` prints the tables below,
`audio_onset_bench --memory-10h` is the ctest `audio_onset_memory`).

## What was measured

**Synthetic sound only**, generated in code (`tests/core/synthetic_audio.h`, seeded, deterministic). No recorded or downloaded media.

- **Basic families (98 clips):** kick-like drum hits (a falling sine with a short noise click) on a beat grid at 50-180 BPM
  (steps of 10) over a quiet chord pad. Variants: 20 ms jitter, white noise, no pad, off-beat hats, 10 s of silence inside, a
  mixture. Plus speech-like syllables, steady noise, silence and a pad alone, which have no tempo.
- **Harder families (228 clips, added after the review):** 19 tempi from 50 to 176 BPM (steps of 7), 5 ms jitter:
  - short broadband click kicks with noise hats on the off-beat at 0.1-0.6 of the kick's level;
  - drum kicks with off-beat hats and noise;
  - bass-heavy mixes (a long 45 Hz sub under every kick, with 8th or off-beat hats);
  - swing (the off-beat hat at 0.64 of the beat);
  - hats on every 8th and every 16th;
  - syncopation (an extra hit at 3/4 of half the beats).
- **Gate limits:** the drum family scaled down by 30-50 dB (quiet masters), and music with speech-like syllables over it.
- **Before** = what the code had: the Player's "BPM from audio" (`EstimateBpmFromLoudness`, median gap between peaks of
  the 50 ms loudness envelope), its loudness peaks as onsets, and a metronome at that tempo from the first peak as beats.
  Plus a **trivial baseline**: the strongest autocorrelation lag of the same envelope, 40-240 BPM, no prior. For the harder
  families and the gate, the first version of this PR (0f370d8) is shown as well.
- **After** = `OnsetCurve` (log band flux, 512/256 at 11025 Hz), `PickOnsets`, `EstimateTempo` (pulse comb, prior 72 BPM,
  one octave), `TrackBeats` (Ellis DP, tightness 100), `AssessTempo`.
- Tempo: the share of clips within 4 % of the truth, strict and octave-corrected (x1/2, x1, x2). Onset F-measure at +-50 ms,
  beat F-measure at +-70 ms. Machine: a 4 vCPU Xeon @ 2.1 GHz shared with other jobs (load average 15-25 during the run),
  Release build, GCC 13. CPU times are indicative only.

## The failure mode the review found, and the fix

The first version picked a tempo among all lags, favoured by the 72 BPM prior. It penalised only a pulse at 1/2 or 1/3 of
the lag. With hats on the off-beat, the strongest pulse is half the beat. Five of those pulses (2.5 beats, so 0.4 x the
tempo) or 2.5 of them with 16ths (0.8 x) have no pulse at their half or third, so nothing was subtracted, and the prior
preferred the slow lag. The result was **2/5 of the tempo from about 106 BPM on, and 4/5 or 1/3 with 16th hats**. Octave
correction cannot fold these ratios back.

On my families the first version scored **73.7 %** octave-corrected on the 228 harder clips, against 91.2 % for the trivial
baseline. The reviewer's own generator gave 36.8-42.1 % with hats at 0.2-0.3.

The fix: first find the **pulse**, the shortest period (down to 320 BPM) whose comb over its multiples (mean R at 1..6
times it) reaches 75 % of the best comb. The tempo is then chosen only among duple multiples of the pulse (x1, 2, 4, ...).
Triple multiples (x3, 6, ...) are allowed with a 0.9 handicap. Octave folding and the prior choose among these.

Result: **100 %** on all 228 harder clips, no ratio misses left, and beat F 0.704 -> 0.813. On the basic table,
octave-corrected rises 98.0 -> 100 %, strict stays the same and beat F goes 0.833 -> 0.837. The regression tests
(`tests/core/audio_onsets_test.cpp`) fail on the first version (2/5 at 106, 120 and 148 BPM; 4/5 at 78; 1/3 at 155 and 176)
and pass now.

**Not reproduced:** the reviewer's bass-heavy 1/3 errors (141, 155 and 176 BPM) do not happen with my bass-heavy generator,
before or after the fix. Those tempi are tested anyway. The same 1/3 errors did appear with 16th hats, and those are fixed.

## Reading the numbers

- **Fast songs come out at half tempo, by design.** The 72 BPM heart prior folds 140-180 BPM down to 70-90, the stroke
  tempo the Generator wants. So strict accuracy is lower than octave-corrected (62.2 % on the basic table). Where the truth
  is in the heart band (at most 130 BPM), strict accuracy rises from 90.5 % to 95.2 %. With off-beat hats at 50-130 BPM, a
  third of the clips choose the hats' double tempo (an octave).
- **Noise 0.15:** the old estimate fails (35.7 %), the new one is right on every clip. The gate still trusts only 35.7 %
  of them: mostly steady noise with a confidence under 0.6, deliberately conservative.
- **Gate:** it trusts no speech-like, steady-noise, silent or pad-only clip, and 90.8 % of the basic tempo clips.
  - Silence is now absolute at -80 dBFS, or a block 35 dB under the clip's 90th-percentile block. The first version used an
    absolute -60 dBFS, which counted quiet masters as silence: 78.9 % trusted at -40 dB and 0 % at -50 dB. Now 100 % are
    trusted at both levels.
  - Music with speech over it is trusted on every clip with my generator. The reviewer reported 0 of 8 with his, so this
    is generator-dependent and unverified on real material.
- **Tempo changes:** inside one tempo, every 10 s window is right (octave-corrected). A whole clip with a change gives
  the dominant tempo.
  - In 1 of 8 clips (144 -> 180 BPM) it gives a quarter of the tempo instead.
  - The review saw a value between the two tempi for x1.2 changes above about 106 BPM.
  - For a clip with a tempo change, use windows, not the whole-clip estimate.
- **Memory:** 10 hours streamed in 1 s chunks peak at 23.5 KiB of heap for the whole analysis (bound 32 KiB, checked by
  ctest with a counting allocator), with the tempo still right (75 BPM). `Estimate` now uses a 2 KB local buffer instead
  of writing into the object, so it is const and thread-safe. Keeping the outputs costs 4 bytes per onset value (172 B per
  second of sound); `TrackBeats` needs 16 bytes per value while it runs.
- **CPU:** about 0.4-0.7 ms per second of sound for the onset curve (FFT, bands, gate) and under 0.01 ms for tempo and
  beats. That is a 2-hour film in a few seconds on one core, on top of the decode.
- **Rhythmic gap fill:**
  - It ties the linear fill on the Script Doctor score everywhere.
  - It wins clearly where strokes go on through a hole (gap r 0.278 -> 0.889, MAE 14.8 -> 10.8).
  - It loses where the script really holds in the hole (gap r 0.488 -> 0.425, MAE 11.4 -> 27.7).
  - With only the heart rhythm (a sparse script), r improves but r0 and MAE do not.
  - The acceptance "at least as good on the existing metrics" therefore fails on the holding case, so
    **`generator.rhythmic_fill` stays off**. The script alone cannot tell a hold from lost tracking. The next step is to decide
    per hole from the sound or the picture.
- **Heart-rhythm grid:** the drift's knots are now 40 beats apart instead of 10 s. Two adjacent beat intervals differ by at
  most 3v / (40 (1 - v)^2): 0.93 % at 10 % variation and 0.24 % at the default 3 %, at any tempo from 40 to 140 BPM. This is
  tested at 40/140/72 BPM with 10 % and over 60 seeds. With 10 s knots it was up to 4.5 % (40 BPM, 10 %).

## Measured tables (audio_onset_bench, 9 Nov 2026)

### Onsets, tempo, beats (30 s clips, 11025 Hz)

Tempo: % of clips within 4 % (strict / octave-corrected x1/2 x1 x2). Onset F at 50 ms, beat F at 70 ms, before -> after. Reliable: % of clips AssessTempo trusts.

| family | clips | existing (loudness peaks) | trivial ACF | after (EstimateTempo) | strict, truth <= 130 BPM: existing -> after | onset F | beat F | reliable % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| clean                  |  14 |  78.6 /  78.6 |  78.6 /  92.9 |  64.3 / 100.0 | 100.0 -> 100.0 | 1.000 -> 0.990 | 0.597 -> 0.875 | 100.0 |
| jitter 20 ms           |  14 |  78.6 /  78.6 |  64.3 /  92.9 |  64.3 / 100.0 | 100.0 -> 100.0 | 1.000 -> 0.990 | 0.605 -> 0.875 | 100.0 |
| noise 0.15             |  14 |  35.7 /  35.7 |  78.6 /  92.9 |  64.3 / 100.0 |  33.3 -> 100.0 | 0.779 -> 0.885 | 0.263 -> 0.845 |  35.7 |
| sparse (no pad)        |  14 |  78.6 /  78.6 |  78.6 /  92.9 |  64.3 / 100.0 | 100.0 -> 100.0 | 1.000 -> 1.000 | 0.597 -> 0.874 | 100.0 |
| off-beat hats          |  14 |  78.6 /  78.6 |  78.6 /  92.9 |  42.9 / 100.0 | 100.0 ->  66.7 | 0.669 -> 0.989 | 0.597 -> 0.804 | 100.0 |
| 10 s silence inside    |  14 |  78.6 /  78.6 |  78.6 /  92.9 |  71.4 / 100.0 | 100.0 -> 100.0 | 1.000 -> 0.974 | 0.498 -> 0.718 | 100.0 |
| jitter+noise+hats      |  14 |  78.6 /  78.6 |  78.6 /  92.9 |  64.3 / 100.0 | 100.0 -> 100.0 | 0.670 -> 0.701 | 0.597 -> 0.865 | 100.0 |
| all tempo families     |  98 |  72.4 /  72.4 |  76.5 /  92.9 |  62.2 / 100.0 |  90.5 ->  95.2 | 0.874 -> 0.933 | 0.536 -> 0.837 |  90.8 |
| speech-like (6 seeds)  |   6 |       -       |       -       |       -       |       -        | 0.252 -> 1.000 |       -        |   0.0 |
| steady noise           |   3 |       -       |       -       |       -       |       -        | 0.000 -> 0.000 |       -        |   0.0 |
| silence                |   3 |       -       |       -       |       -       |       -        | 1.000 -> 1.000 |       -        |   0.0 |
| chord pad only         |   3 |       -       |       -       |       -       |       -        | 0.000 -> 0.000 |       -        |   0.0 |

### Harder rhythms (30 s clips, 19 tempi 50-176 BPM, 5 ms jitter)

Octave-corrected tempo within 4 % (strict for after in brackets); beat F at 70 ms before -> after; the after misses by ratio to the truth.

| family | existing | trivial ACF | after | beat F | after misses |
|---|---:|---:|---:|---:|---|
| click kick, off-beat hats 0.1 | 31.6 | 94.7 | 100.0 (57.9) | 0.298 -> 0.850 | - |
| click kick, off-beat hats 0.2 | 31.6 | 84.2 | 100.0 (42.1) | 0.298 -> 0.798 | - |
| click kick, off-beat hats 0.3 | 31.6 | 84.2 | 100.0 (36.8) | 0.298 -> 0.782 | - |
| click kick, off-beat hats 0.4 | 31.6 | 84.2 | 100.0 (31.6) | 0.298 -> 0.765 | - |
| click kick, off-beat hats 0.6 | 31.6 | 94.7 | 100.0 (31.6) | 0.298 -> 0.660 | - |
| drum kick, off-beat hats 0.4 + noise | 84.2 | 100.0 | 100.0 (57.9) | 0.476 -> 0.844 | - |
| bass-heavy (sub + kick + 8th hats) | 84.2 | 94.7 | 100.0 (63.2) | 0.477 -> 0.864 | - |
| bass-heavy, click kick, off-beat hats | 84.2 | 94.7 | 100.0 (57.9) | 0.466 -> 0.848 | - |
| swing (off-beat at 0.64) | 0.0 | 100.0 | 100.0 (68.4) | 0.312 -> 0.876 | - |
| hats on every 8th | 31.6 | 84.2 | 100.0 (31.6) | 0.293 -> 0.766 | - |
| hats on every 16th | 10.5 | 84.2 | 100.0 (57.9) | 0.313 -> 0.846 | - |
| syncopation (extra hit at 3/4) | 31.6 | 94.7 | 100.0 (63.2) | 0.349 -> 0.860 | - |
| **all 228 clips** | 40.4 | 91.2 | 100.0 | 0.348 -> 0.813 | |

### Gate limits (19 tempi 50-176 BPM, drum kick + pad, 30 s)

| case | tempo right (octave) | trusted by AssessTempo |
|---|---:|---:|
| normal level (peaks about -4 dBFS) | 100.0 % | 100.0 % |
| quiet master, -30 dB | 100.0 % | 100.0 % |
| quiet master, -40 dB | 100.0 % | 100.0 % |
| quiet master, -50 dB | 100.0 % | 100.0 % |
| music with speech over it | 100.0 % | 100.0 % |

### Tempo changes (60 s clips, x1.25 at 30 s; 10 s windows inside one tempo)

| windows | existing strict / octave | after strict / octave | after, whole clip matches either tempo (octave) |
|---:|---:|---:|---:|
| 80 | 68.8 / 68.8 | 76.2 / 100.0 | 7 of 8 |

Whole clips: 56->70: 70.0, 64->80: 80.0, 72->90: 90.0, 80->100: 100.0, 96->120: 120.1, 112->140: 139.0, 128->160: 79.9, 144->180: 45.6 (neither tempo nor its octave).

### Cost (10 min clip, 11025 Hz, 96 BPM with hats, noise and jitter)

| step | ms per second of sound |
|---|---:|
| before: 50 ms RMS + loudness + EstimateBpmFromLoudness | 0.0511 |
| OnsetCurve (FFT, band flux, gate) | 0.8337 |
| EstimateTempo | 0.0086 |
| TrackBeats | 0.0042 |

Peak heap of the after analysis on 10 min, outputs kept: 591.6 KiB (onset curve 25840 values, 960 beats).

### 10 hours streamed (11025 Hz, 1 s chunks, outputs drained)

| frames | peak heap of the analysis | bound | tempo | wall time | ms per second of sound |
|---:|---:|---:|---:|---:|---:|
| 1550391 | 23.5 KiB | 32 KiB | 75.0 BPM | 22.2 s | 0.6157 |

### Gap fill: linear (default) vs heart rhythm (generator.rhythmic_fill)

120 s stroke scripts, 4 holes of 1.5-5 s per script, ImproveScript fill_gaps with the auto threshold, no audio tempo.
Doctor = EvaluateScriptQuality score (existing); gap r = BestLagCorrelation inside the holes (existing, +-1 s lag, best orientation; undefined = 0);
gap r0 = Pearson at lag 0 and gap MAE = mean |position error| in the holes on a 50 ms grid (new, this bench).

| clips | n | Doctor linear -> rhythmic | gap r linear -> rhythmic | gap r0 linear -> rhythmic | gap MAE linear -> rhythmic |
|---|---:|---:|---:|---:|---:|
| strokes through the holes | 18 | 1.000 -> 1.000 | 0.278 -> 0.889 | -0.104 -> 0.829 | 14.8 -> 10.8 |
| script holds in the holes | 18 | 1.000 -> 1.000 | 0.488 -> 0.425 | 0.379 -> -0.020 | 11.4 -> 27.7 |
| sparse script (heart rhythm) | 18 | 0.994 -> 1.000 | 0.318 -> 0.468 | -0.006 -> -0.067 | 15.5 -> 20.1 |

Rhythmic at least as good as linear on the existing metrics (Doctor, gap r) where the script strokes or holds: NO

Peak RSS of the whole bench process: 33772 KiB

### Harder rhythms, first version of this PR (0f370d8) (30 s clips, 19 tempi 50-176 BPM, 5 ms jitter)

Octave-corrected tempo within 4 % (strict for after in brackets); beat F at 70 ms before -> after; the after misses by ratio to the truth.

| family | existing | trivial ACF | after | beat F | after misses |
|---|---:|---:|---:|---:|---|
| click kick, off-beat hats 0.1 | 31.6 | 94.7 | 100.0 (57.9) | 0.298 -> 0.850 | - |
| click kick, off-beat hats 0.2 | 31.6 | 84.2 | 63.2 (42.1) | 0.298 -> 0.663 | 120: 2/5, 127: 2/5, 134: 2/5, 141: 2/5, 155: 2/5, 162: 2/5, 169: 2/5 |
| click kick, off-beat hats 0.3 | 31.6 | 84.2 | 42.1 (31.6) | 0.298 -> 0.550 | 106: 2/5, 113: 2/5, 120: 2/5, 127: 2/5, 134: 2/5, 141: 2/5, 148: 2/5, 155: 2/5, 162: 2/5, 169: 2/5, 176: 2/5 |
| click kick, off-beat hats 0.4 | 31.6 | 84.2 | 42.1 (26.3) | 0.298 -> 0.532 | 106: 2/5, 113: 2/5, 120: 2/5, 127: 2/5, 134: 2/5, 141: 2/5, 148: 2/5, 155: 2/5, 162: 2/5, 169: 2/5, 176: 2/5 |
| click kick, off-beat hats 0.6 | 31.6 | 94.7 | 42.1 (26.3) | 0.298 -> 0.531 | 106: 2/5, 113: 2/5, 120: 2/5, 127: 2/5, 134: 2/5, 141: 2/5, 148: 2/5, 155: 2/5, 162: 2/5, 169: 2/5, 176: 2/5 |
| drum kick, off-beat hats 0.4 + noise | 84.2 | 100.0 | 100.0 (52.6) | 0.476 -> 0.827 | - |
| bass-heavy (sub + kick + 8th hats) | 84.2 | 94.7 | 100.0 (63.2) | 0.477 -> 0.864 | - |
| bass-heavy, click kick, off-beat hats | 84.2 | 94.7 | 100.0 (52.6) | 0.466 -> 0.830 | - |
| swing (off-beat at 0.64) | 0.0 | 100.0 | 100.0 (63.2) | 0.312 -> 0.858 | - |
| hats on every 8th | 31.6 | 84.2 | 42.1 (26.3) | 0.293 -> 0.538 | 106: 2/5, 113: 2/5, 120: 2/5, 127: 2/5, 134: 2/5, 141: 2/5, 148: 2/5, 155: 2/5, 162: 2/5, 169: 2/5, 176: 2/5 |
| hats on every 16th | 10.5 | 84.2 | 52.6 (21.1) | 0.313 -> 0.551 | 50: 4/5, 57: 4/5, 64: 4/5, 71: 4/5, 78: 4/5, 85: 4/5, 92: 4/5, 155: 1/3, 176: 1/3 |
| syncopation (extra hit at 3/4) | 31.6 | 94.7 | 100.0 (63.2) | 0.349 -> 0.860 | - |
| **all 228 clips** | 40.4 | 91.2 | 73.7 | 0.348 -> 0.704 | |

### Gate limits, first version of this PR (0f370d8) (19 tempi 50-176 BPM, drum kick + pad, 30 s)

| case | tempo right (octave) | trusted by AssessTempo |
|---|---:|---:|
| normal level (peaks about -4 dBFS) | 100.0 % | 100.0 % |
| quiet master, -30 dB | 100.0 % | 100.0 % |
| quiet master, -40 dB | 100.0 % | 78.9 % |
| quiet master, -50 dB | 100.0 % | 0.0 % |
| music with speech over it | 100.0 % | 100.0 % |

## Before wiring into the app

- `EstimateTempo` should go into the app (for "BPM from audio", or the Generator) only after a check on real music. The
  ratio errors are fixed on every synthetic family here, but these families were written to cover the failure the review
  found. A real-music set may find other patterns.
- The thresholds were all set on these signals only: the gate (-80 dBFS absolute, 35 dB relative, flatness 0.30, quiet share
  0.25, confidence 0.30 / 0.60) and the pulse comb (75 % of the best, 320 BPM, triple handicap 0.9).

## Not measured

- Real music, speech or film sound: every number above is synthetic.
- Windows / MSVC: built and run on Linux only (GCC 13; clean under clang `-Wconversion`), not compiled with MSVC here.
- The app does not yet decode into `OnsetCurve`. The Player's "BPM from audio" still uses the loudness estimate.
