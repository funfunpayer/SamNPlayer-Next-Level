# Where the engine spends its time (18 Oct)

**Rule (CONTRIBUTING 18):** measure before optimising. This profile was taken with callgrind (inclusive cost). The run was `TrackBox` with all options on: camera compensation, scene cuts and memory, on the synthetic 320×240 clip with 200 frames. Wall time on the panning clip is 25.6 ms per frame.

| Part | Share |
|---|---:|
| CSRT `Update` (OpenCV) | 93 % |
| of it: `dft` / `idft` | 50 % |
| of it: `resize` | 10 % |
| Camera compensation (`goodFeaturesToTrack`, LK, RANSAC) | 5 % |
| Scene cuts, memory, guards, our own loop | about 1 % |

**What follows**

- **No micro-optimising of our own code** for speed. It is about 6 % of the time, so even halving it saves 3 %.
- **The levers, each measured with `samn_compare` before it becomes a default (P3.9):**
  1. **Parallel work (D-142)** where frames do not depend on each other: decoding next to tracking, several trackers (P3.4), audio next to video, several clips at once.
  2. **Tracking on a smaller frame** (scaled box and frame). This is fast but changes the result, so it needs the fidelity gate, not a guess.
  3. **OpenCV built with IPP/TBB** on Windows (vcpkg features). This is the same algorithm, faster FFT and resize; it is measured as speed only.
- **Our own hot loops** still follow the rules below, because they cost nothing and keep the code clean.
