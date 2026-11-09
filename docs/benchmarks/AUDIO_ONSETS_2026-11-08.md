# Audio onsets, tempo, beats and the heart rhythm: before and after (8 Nov 2026)

Decision: D-168. Code: `core/include/samn/audio_onsets.h`, `audio_tempo.h`, `base_rhythm.h`, `improve.h` (`FillGapsRhythmic`).
Bench: `tests/bench/audio_onset_bench.cpp` (built with the tests; `audio_onset_bench` prints the tables below,
`audio_onset_bench --memory-10h` is the ctest `audio_onset_memory`).

## What was measured

**Synthetic sound only**, generated in code (`tests/core/synthetic_audio.h`, seeded, deterministic): kick-like drum hits
on a beat grid at 50-180 BPM (steps of 10) over a quiet chord pad, with variants for 20 ms jitter, white noise,
no pad (sparse hits), off-beat hats, 10 s of silence inside the clip, a tempo change of x1.25 halfway, and mixtures;
speech-like syllables (harmonics of a gliding F0 through two formants, irregular syllable and pause lengths); steady noise;
silence; a chord pad alone. No recorded or downloaded media.

- **Before** = what the code had: the Player's "BPM from audio", `EstimateBpmFromLoudness` (median gap between peaks of
  the 50 ms loudness envelope), its loudness peaks as onsets and, for beats, a metronome at that tempo from the first peak
  (no beat tracker existed). Plus a **trivial baseline**: the strongest autocorrelation lag of the same envelope, 40-240 BPM, no prior.
- **After** = `OnsetCurve` (log band flux, 512/256 at 11025 Hz), `PickOnsets`, `EstimateTempo` (prior 72 BPM, one octave),
  `TrackBeats` (Ellis DP, tightness 100), `AssessTempo`.
- Tempo: share of clips within 4 % of the truth, strict and octave-corrected (x1/2, x1, x2). Onset F-measure at +-50 ms,
  beat F-measure at +-70 ms (greedy one-to-one matching). Machine: 4 vCPU Xeon @ 2.1 GHz shared with other jobs
  (load average about 10 during the run), Release build, GCC 13; CPU times are indicative only.

## Reading the numbers

- **Octave-corrected tempo 72.4 % -> 98.0 %**, onset F 0.874 -> 0.933, beat F 0.536 -> 0.833 over the 98 tempo clips.
- **Strict tempo drops (72.4 % -> 62.2 %) on purpose**: the 72 BPM heart prior folds 140-180 BPM to half (70-90), the
  stroke tempo the Generator wants. Where the truth is in the heart band (<= 130 BPM) strict accuracy rises 90.5 % -> 95.2 %.
  With off-beat hats at 50-130 BPM a third of the clips pick the hats' double tempo (an octave, counted in the octave column).
- **Noise 0.15**: the old estimate fails (35.7 %); the new one is right on 92.9 %, but the gate calls most of those clips
  unreliable (mostly steady noise and a confidence under 0.6): deliberately conservative.
- **Gate**: no speech-like, steady-noise, silent or pad-only clip is called reliable (0 %); 91.8 % of the tempo clips are.
- **Memory**: 10 hours streamed in 1 s chunks peak at 20.9 KiB of heap for the whole analysis (bound 32 KiB, checked by
  ctest with a counting allocator), the tempo still right (75 BPM). Keeping the outputs costs 4 bytes per onset value
  (172 B per second of sound) and `TrackBeats` 16 bytes per value while it runs.
- **CPU**: about 0.4 ms per second of sound for the onset curve (FFT, bands, gate), under 0.01 ms for tempo and beats:
  a 2-hour film in about 3 s on one core, on top of the decode.
- **Rhythmic gap fill**: ties linear on the Script Doctor score everywhere and wins clearly where strokes go on through a
  hole (gap r 0.278 -> 0.889, MAE 14.8 -> 10.8), but loses where the script really holds in the hole (gap r 0.488 -> 0.425,
  MAE 11.4 -> 27.7); with only the heart rhythm (sparse script) r improves but r0 and MAE do not. The acceptance
  "at least as good on the existing metrics" therefore fails on the holding case: **`generator.rhythmic_fill` stays off**.
  The script alone cannot tell a hold from lost tracking; the next step is to decide per hole from the sound
  (`AssessTempo` / onsets inside the hole) or the picture.

## Measured tables (audio_onset_bench, 8 Nov 2026)

### Onsets, tempo, beats (30 s clips, 11025 Hz)

Tempo: % of clips within 4 % (strict / octave-corrected x1/2 x1 x2). Onset F at 50 ms, beat F at 70 ms, before -> after. Reliable: % of clips AssessTempo trusts.

| family | clips | existing (loudness peaks) | trivial ACF | after (EstimateTempo) | strict, truth <= 130 BPM: existing -> after | onset F | beat F | reliable % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| clean                  |  14 |  78.6 /  78.6 |  78.6 /  92.9 |  64.3 / 100.0 | 100.0 -> 100.0 | 1.000 -> 0.990 | 0.597 -> 0.875 | 100.0 |
| jitter 20 ms           |  14 |  78.6 /  78.6 |  64.3 /  92.9 |  64.3 / 100.0 | 100.0 -> 100.0 | 1.000 -> 0.990 | 0.605 -> 0.875 | 100.0 |
| noise 0.15             |  14 |  35.7 /  35.7 |  78.6 /  92.9 |  64.3 /  92.9 |  33.3 -> 100.0 | 0.779 -> 0.884 | 0.263 -> 0.834 |  42.9 |
| sparse (no pad)        |  14 |  78.6 /  78.6 |  78.6 /  92.9 |  64.3 / 100.0 | 100.0 -> 100.0 | 1.000 -> 1.000 | 0.597 -> 0.874 | 100.0 |
| off-beat hats          |  14 |  78.6 /  78.6 |  78.6 /  92.9 |  42.9 /  92.9 | 100.0 ->  66.7 | 0.669 -> 0.989 | 0.597 -> 0.793 | 100.0 |
| 10 s silence inside    |  14 |  78.6 /  78.6 |  78.6 /  92.9 |  71.4 / 100.0 | 100.0 -> 100.0 | 1.000 -> 0.974 | 0.498 -> 0.718 | 100.0 |
| jitter+noise+hats      |  14 |  78.6 /  78.6 |  78.6 /  92.9 |  64.3 / 100.0 | 100.0 -> 100.0 | 0.670 -> 0.701 | 0.597 -> 0.865 | 100.0 |
| all tempo families     |  98 |  72.4 /  72.4 |  76.5 /  92.9 |  62.2 /  98.0 |  90.5 ->  95.2 | 0.874 -> 0.933 | 0.536 -> 0.833 |  91.8 |
| speech-like (6 seeds)  |   6 |       -       |       -       |       -       |       -        | 0.252 -> 1.000 |       -        |   0.0 |
| steady noise           |   3 |       -       |       -       |       -       |       -        | 0.000 -> 0.000 |       -        |   0.0 |
| silence                |   3 |       -       |       -       |       -       |       -        | 1.000 -> 1.000 |       -        |   0.0 |
| chord pad only         |   3 |       -       |       -       |       -       |       -        | 0.000 -> 0.000 |       -        |   0.0 |

### Tempo changes (60 s clips, x1.25 at 30 s; 10 s windows inside one tempo)

| windows | existing strict / octave | after strict / octave | after, whole clip matches either tempo (octave) |
|---:|---:|---:|---:|
| 60 | 83.3 / 83.3 | 91.7 / 100.0 | 6 of 6 |

### Cost (10 min clip, 11025 Hz, 96 BPM with hats, noise and jitter)

| step | ms per second of sound |
|---|---:|
| before: 50 ms RMS + loudness + EstimateBpmFromLoudness | 0.0316 |
| OnsetCurve (FFT, band flux, gate) | 0.4053 |
| EstimateTempo | 0.0047 |
| TrackBeats | 0.0025 |

Peak heap of the after analysis on 10 min, outputs kept: 591.6 KiB (onset curve 25840 values, 960 beats).

### 10 hours streamed (11025 Hz, 1 s chunks, outputs drained)

| frames | peak heap of the analysis | bound | tempo | wall time | ms per second of sound |
|---:|---:|---:|---:|---:|---:|
| 1550391 | 20.9 KiB | 32 KiB | 75.0 BPM | 13.9 s | 0.3857 |

### Gap fill: linear (default) vs heart rhythm (generator.rhythmic_fill)

120 s stroke scripts, 4 holes of 1.5-5 s per script, ImproveScript fill_gaps with the auto threshold, no audio tempo.
Doctor = EvaluateScriptQuality score (existing); gap r = BestLagCorrelation inside the holes (existing, +-1 s lag, best orientation; undefined = 0);
gap r0 = Pearson at lag 0 and gap MAE = mean |position error| in the holes on a 50 ms grid (new, this bench).

| clips | n | Doctor linear -> rhythmic | gap r linear -> rhythmic | gap r0 linear -> rhythmic | gap MAE linear -> rhythmic |
|---|---:|---:|---:|---:|---:|
| strokes through the holes | 18 | 1.000 -> 1.000 | 0.278 -> 0.889 | -0.104 -> 0.829 | 14.8 -> 10.8 |
| script holds in the holes | 18 | 1.000 -> 1.000 | 0.488 -> 0.425 | 0.379 -> -0.020 | 11.4 -> 27.7 |
| sparse script (heart rhythm) | 18 | 0.994 -> 1.000 | 0.318 -> 0.469 | -0.006 -> -0.059 | 15.5 -> 20.1 |

Rhythmic at least as good as linear on the existing metrics (Doctor, gap r) where the script strokes or holds: NO

Peak RSS of the whole bench process: 32796 KiB

## Not measured

- Real music, speech or film sound: every number above is synthetic. The gate's thresholds (silence -60 dBFS, flatness 0.30,
  quiet share 0.25, confidence 0.30 / 0.60) were set on these signals only.
- Windows / MSVC: built and run on Linux (GCC 13; clang `-Wconversion` clean); not compiled with MSVC here.
- The app does not yet decode into `OnsetCurve`; the Player's "BPM from audio" still uses the loudness estimate.
