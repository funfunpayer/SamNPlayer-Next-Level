# Audio onsets, tempo, beats and the heart rhythm: before and after (8 Nov 2026, revised 9 Nov after review, follow-up 12 Nov)

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

## Measured tables (audio_onset_bench, 12 Nov 2026, after the follow-up)

### Onsets, tempo, beats (30 s clips, 11025 Hz)

Tempo: % of clips within 4 % (strict / octave-corrected x1/2 x1 x2). Onset F at 50 ms, beat F at 70 ms, before -> after. Reliable: % of clips AssessTempo trusts.

| family | clips | existing (loudness peaks) | trivial ACF | after (EstimateTempo) | strict, truth <= 130 BPM: existing -> after | onset F | beat F | reliable % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| clean                  |  14 |  78.6 /  78.6 |  78.6 /  92.9 |  64.3 / 100.0 | 100.0 -> 100.0 | 1.000 -> 0.990 | 0.597 -> 0.875 | 100.0 |
| jitter 20 ms           |  14 |  78.6 /  78.6 |  64.3 /  92.9 |  64.3 / 100.0 | 100.0 -> 100.0 | 1.000 -> 0.990 | 0.605 -> 0.875 | 100.0 |
| noise 0.15             |  14 |  35.7 /  35.7 |  78.6 /  92.9 |  64.3 / 100.0 |  33.3 -> 100.0 | 0.779 -> 0.884 | 0.263 -> 0.845 |  35.7 |
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

Octave-corrected tempo within 4 % (strict for after in brackets); onset F at 50 ms and beat F at 70 ms before -> after; the after misses by ratio to the truth.

| family | existing | trivial ACF | after | onset F | beat F | after misses |
|---|---:|---:|---:|---:|---:|---|
| click kick, off-beat hats 0.1 | 31.6 | 94.7 | 100.0 (57.9) | 1.000 -> 0.948 | 0.298 -> 0.850 | - |
| click kick, off-beat hats 0.2 | 31.6 | 84.2 | 100.0 (42.1) | 1.000 -> 0.999 | 0.298 -> 0.798 | - |
| click kick, off-beat hats 0.3 | 31.6 | 84.2 | 100.0 (36.8) | 1.000 -> 1.000 | 0.298 -> 0.782 | - |
| click kick, off-beat hats 0.4 | 31.6 | 84.2 | 100.0 (31.6) | 1.000 -> 1.000 | 0.298 -> 0.765 | - |
| click kick, off-beat hats 0.6 | 31.6 | 94.7 | 100.0 (31.6) | 1.000 -> 1.000 | 0.298 -> 0.660 | - |
| drum kick, off-beat hats 0.4 + noise | 84.2 | 100.0 | 100.0 (57.9) | 0.669 -> 0.924 | 0.476 -> 0.844 | - |
| bass-heavy (sub + kick + 8th hats) | 84.2 | 94.7 | 100.0 (63.2) | 0.580 -> 0.902 | 0.477 -> 0.864 | - |
| bass-heavy, click kick, off-beat hats | 84.2 | 94.7 | 100.0 (57.9) | 0.596 -> 0.947 | 0.466 -> 0.848 | - |
| swing (off-beat at 0.64) | 0.0 | 100.0 | 100.0 (68.4) | 1.000 -> 1.000 | 0.312 -> 0.876 | - |
| hats on every 8th | 31.6 | 84.2 | 100.0 (31.6) | 1.000 -> 1.000 | 0.293 -> 0.766 | - |
| hats on every 16th | 10.5 | 84.2 | 100.0 (57.9) | 0.904 -> 0.962 | 0.313 -> 0.846 | - |
| syncopation (extra hit at 3/4) | 31.6 | 94.7 | 100.0 (63.2) | 0.993 -> 0.992 | 0.349 -> 0.860 | - |
| **all 228 clips** | 40.4 | 91.2 | 100.0 | 0.895 -> 0.973 | 0.348 -> 0.813 | |

### Polymetric layers (limits) (30 s clips, 19 tempi 50-176 BPM, 5 ms jitter)

Octave-corrected tempo within 4 % (strict for after in brackets); onset F at 50 ms and beat F at 70 ms before -> after; the after misses by ratio to the truth.

| family | existing | trivial ACF | after | onset F | beat F | after misses |
|---|---:|---:|---:|---:|---:|---|
| tom layer at 2/3 of the tempo | 57.9 | 52.6 | 84.2 (68.4) | 0.886 -> 0.879 | 0.401 -> 0.857 | 162: 1/3, 169: 1/3, 176: 1/3 |
| tom layer at 3/4 of the tempo | 26.3 | 68.4 | 89.5 (52.6) | 0.913 -> 0.912 | 0.336 -> 0.802 | 169: other, 176: other |
| tom layer at 1/3 of the tempo | 84.2 | 84.2 | 100.0 (68.4) | 0.854 -> 0.847 | 0.476 -> 0.886 | - |
| tom layer at 3/2 of the tempo | 0.0 | 73.7 | 73.7 (0.0) | 0.869 -> 0.875 | 0.334 -> 0.588 | 50: 3/2, 57: 3/2, 64: 3/2, 71: 3/2, 78: 3/2 |
| tom layer at 4/3 of the tempo | 15.8 | 0.0 | 36.8 (36.8) | 0.877 -> 0.900 | 0.336 -> 0.651 | 57: 4/3, 78: 2/3, 85: 2/3, 120: 2/3, 127: 1/3, 134: 1/3, 141: 1/3, 148: 1/3, 155: 1/3, 162: 1/3, 169: 1/3, 176: 1/3 |
| tom layer at 5/4 of the tempo | 10.5 | 10.5 | 57.9 (10.5) | 0.882 -> 0.919 | 0.300 -> 0.530 | 50: 5/4, 57: 5/4, 64: 5/4, 71: 5/4, 78: 5/4, 162: other, 169: other, 176: other |
| two tempi summed (x4/3, second at 0.8) | 5.3 | 0.0 | 21.1 (21.1) | 0.896 -> 0.904 | 0.343 -> 0.551 | 50: 4/3, 57: 4/3, 64: 4/3, 78: 2/3, 85: 2/3, 92: 2/3, 120: 1/3, 127: 1/3, 134: 1/3, 141: 1/3, 148: 1/3, 155: 1/3, 162: 1/3, 169: 1/3, 176: 1/3 |
| **all 133 clips** | 28.6 | 41.4 | 66.2 | 0.883 -> 0.891 | 0.361 -> 0.695 | |

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
| before: 50 ms RMS + loudness + EstimateBpmFromLoudness | 0.0379 |
| OnsetCurve (FFT, band flux, gate) | 0.3770 |
| EstimateTempo | 0.0080 |
| TrackBeats | 0.0042 |

Peak heap of the after analysis on 10 min, outputs kept: 591.6 KiB (onset curve 25840 values, 960 beats).

### 10 hours streamed (11025 Hz, 1 s chunks, outputs drained)

| frames | peak heap of the analysis | bound | tempo | wall time | ms per second of sound |
|---:|---:|---:|---:|---:|---:|
| 1550391 | 23.5 KiB | 32 KiB | 75.0 BPM | 13.5 s | 0.3748 |

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

Peak RSS of the whole bench process: 33628 KiB

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

## Follow-up of 12 Nov: polymetric limits, onset gap, holding fill

**Polymetric layers are a limit, not fixed.** The pulse comb fixes subdivisions of one beat (the harder table: 100 %). A
second layer at another ratio of the tempo is different: the common period of the two layers, or the layer itself, can win.
New family table "Polymetric layers": a tom layer at 0.7 of the kick's level on its own grid, and a second drum track at
0.8 level at 4/3 of the tempo. Octave-corrected, 19 tempi each:

| layer | old Player estimate | trivial ACF | EstimateTempo | misses |
|---|---:|---:|---:|---|
| tom at 2/3 of the tempo | 57.9 % | 52.6 % | 84.2 % | 1/3 at 162-176 BPM |
| tom at 3/4 | 26.3 % | 68.4 % | 89.5 % | 169, 176 BPM |
| tom at 1/3 | 84.2 % | 84.2 % | 100 % | - |
| tom at 3/2 | 0 % | 73.7 % | 73.7 % | locks to the 1.5x layer at 50-78 BPM |
| tom at 4/3 | 15.8 % | 0 % | 36.8 % | 1/3 at 127-176 BPM, 2/3, 4/3 |
| tom at 5/4 | 10.5 % | 10.5 % | 57.9 % | locks to the 5/4 layer at 50-78 BPM; 162-176 BPM |
| two tempi summed (x4/3, second at 0.8) | 5.3 % | 0 % | 21.1 % | the other tempo, 2/3 or 1/3 |

- **The reviewer's generator gave** 2/3: 64 %, 3/4: 91 %, 1/3: 95 %, 3/2: 91 %, two tempi x1.33: 73 %, also all at or
  above the trivial ACF. The second review measured tom layers at 4/3 (26-37 %) and 5/4 (21-26 %) with its own generator,
  worse than the families above; this bench now has both (36.8 % and 57.9 %). With his generator, 3/2 is the one family where the merged code is worse than the first version
  (0f370d8): it locks to the 1.5x layer at 51 and 63 BPM.
- **Two tempi at nearly the same level** are ambiguous by construction. "The tempo" is defined here as the louder one.
- **A tried fix, not adopted:** the pulse must divide the best comb's period by a power of two. It lowered every table:
  basic 100 -> 87.8 %, harder 100 -> 80.7 %, polymetric 73.7 -> 61.1 %, and the 3/2 family only rose 73.7 -> 78.9 %. No
  cheap, principled fix was found, so this is documented only. The header of `audio_tempo.h` now says "for subdivisions of
  one beat".

**Onset minimum gap.** The review's nit fix rounded the 70 ms minimum gap up to whole frames: 4 frames, that is 93 ms at
11025 Hz. That dropped the second of two hits 75 ms apart. Two onsets are now compared as times: never closer than 70 ms
minus half a frame (58 ms at 11025 Hz, 62 ms at 4 kHz, 59 ms at 96 kHz). At 43.07 values per second (11025, 22050 and
44100 Hz) the peak test (a maximum over +-2 values) already keeps peaks 3 values (69.7 ms) apart, so the default 70 ms is
inert there and 58 ms is a bound that is never reached. It matters at other rates and for a longer `wait_ms`. New test: clicks 75 ms apart at 11025 Hz are
both found, and the 4-96 kHz test now checks that bound.

Onset F after the change:
- basic table 0.933 overall; one family moved: noise 0.15, 0.885 -> 0.884 (a noise peak 3 values after a hit is now kept; the 4-value gap had dropped it);
- harder families 0.972 -> 0.973 (16th hats 0.954 -> 0.962, syncopation 0.985 -> 0.992);
- the bass kick + sub family stays 0.902. The reviewer's 0.84 -> 0.74 drop does not reproduce with my generator.

Tempo and beats are unchanged.

**Holding fill = FillGaps.** With `generator.rhythmic_fill` on, the straight-line branch for a holding script stepped
`t < next` instead of FillGaps' `k < dt / step`. When the step did not divide the gap (37 ms, or half the period of a
1.2 Hz audio tempo), that added one point within one step of the anchor. Gaps longer than the threshold but too short for
a stroke (at most 428 ms) were left unfilled. Both now give exactly FillGaps' points, tested over 7 steps and gaps.

FillGaps' rule "the gap must be longer than the step" now applies only to that straight line. The first version of this
follow-up applied it to stroking scripts too: a 700 ms gap in a script stroking every 250 ms got no strokes when the line
step was 714 ms (a 0.7 Hz audio tempo) or 1000 ms. That case is now a test.

A scratch fuzz of 200 000 scripts gave:
- holding scripts: 0 differences from FillGaps;
- stroking scripts with every filled gap longer than a stroke: 0 differences from main;
- the same fuzz against the earlier head c17942a: 8535 differences.

The fill bench is unchanged (its holes are longer), and the default is still off.

## Before wiring into the app

- Since #236 the Player's "BPM from audio" uses `EstimateTempo` when `AssessTempo` trusts it, otherwise the loudness
  estimate (`PickPlaybackAudioBpm`). That wiring is not measured here.
- `EstimateTempo` has not been checked on real music. The ratio errors are fixed on every subdivision family here, but these
  families were written to cover the failure the review found. Polymetric material (above) and real-music sets may still
  give a wrong tempo.
- The thresholds were all set on these signals only: the gate (-80 dBFS absolute, 35 dB relative, flatness 0.30, quiet share
  0.25, confidence 0.30 / 0.60) and the pulse comb (75 % of the best, 320 BPM, triple handicap 0.9).

## Not measured

- Real music, speech or film sound: every number above is synthetic.
- Windows / MSVC: built and run on Linux only (GCC 13; clean under clang `-Wconversion`), not compiled with MSVC here.
- The app path since #236 (decode -> `OnsetCurve` -> `PickPlaybackAudioBpm`).
