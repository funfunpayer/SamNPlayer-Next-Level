# P3.2 whole chain: parity with 0.7

**Result (17 Oct):** video → `TrackBox` → `RunSignal` (profile recipe, default detrend, post-processing, speed limit, clamp) gives **the same funscript actions as 0.7's `GenerateNativeCSRT`**. That holds for every action of 13 runs (4 clips, 4 profiles).

## How
- **C++:** `engine_parity_dump <folder> --pipeline` on the clips of `P3_1_TRACK_PARITY.md`. The tracker uses the generator's defaults: camera compensation, scene cuts and memory on, axis auto, box (100, 40, 120, 160). Then `ApplyDefaultDetrend` without a stroke hint (3000 ms for stroke profiles, none for tj), then `RunSignal`.
- **Go:** a temporary test in a scratch checkout of 0.7 (`main` b502b49) called `GenerateNativeCSRT` with `Profile`, `DetrendWindowMs` = 3000 for stroke profiles, and auto-retry off. It read the actions back from the written .funscript. The test was not committed.
- **Profiles:**
  - **standard:** detrend only;
  - **autotune:** detrend, bandpass 0.5–4 Hz, dynamic range, prominence 0.2, speed cap 400;
  - **weich:** prominence 0.35, peak distance 200 ms, dynamic range;
  - **tj:** no detrend, clamp 20–90.

## Numbers
| clip | profile | actions C++/Go | identical | max |Δpos| | max |Δat| ms |
|---|---|---|---|---|---|
| moving | standard | 18/18 | yes | 0 | 0 |
| moving | autotune | 18/18 | yes | 0 | 0 |
| moving | weich | 18/18 | yes | 0 | 0 |
| moving | tj | 18/18 | yes | 0 | 0 |
| panning | standard | 22/22 | yes | 0 | 0 |
| panning | autotune | 22/22 | yes | 0 | 0 |
| panning | weich | 22/22 | yes | 0 | 0 |
| panning | tj | 22/22 | yes | 0 | 0 |
| cut | standard | 18/18 | yes | 0 | 0 |
| cut | autotune | 18/18 | yes | 0 | 0 |
| cut | weich | 18/18 | yes | 0 | 0 |
| cut | tj | 18/18 | yes | 0 | 0 |
| long | standard | 422/422 | yes | 0 | 0 |

## Not covered yet
- The Quality Doctor on the dense curve, and the signal auto-retry that depends on it (P3.6).
- The stroke preview that feeds the adaptive detrend window. Without a hint, both sides use 3000 ms.
- Writing the .funscript / .samn with metadata (P3.3, Generator page).
- Two-point and multi-point tracking (P3.4).
