# P3.1 single-box tracking: parity with 0.7

**Result (17 Oct):** C++ `samn::engine::TrackBox` and 0.7's Go `trackcv.TrackROI` give the **same box centre on every frame, bit for bit**. That holds for 16 runs (4 clips × 4 option sets), 32 600 frames in total, including a 5-minute clip (GD-11).

## How
1. **Clips:** `engine_parity_dump <folder>` (target in `tests/CMakeLists.txt`, source `tests/engine/parity_dump.cpp`) writes four synthetic clips with `tests/engine/synthetic_clips.h`:
   - moving disc, 8 s;
   - moving disc, 300 s at 0.7 Hz;
   - camera pan, 10 s;
   - hard cut at frame 100, 8 s.

   It tracks each clip with the box (100, 40, 120, 160) and writes the C++ centres as CSV.
2. **Go:** a temporary test in a scratch checkout of 0.7 (`main` b502b49) ran `TrackROI` on the same files with the same options and `CaptureTrajectory`, and wrote the Go centres. The test was not committed to either repository.
3. **Compare:** for each frame, |dx| and |dy| of the centres (y after camera compensation), plus the correlation of y.

Option sets:
- **plain:** everything off;
- **cam:** camera compensation;
- **cutmem:** scene cuts and appearance memory;
- **all:** all three.

Axis y. OpenCV 4.6.0, FFmpeg backend, Linux.

## Numbers
| clip | set | frames C++/Go | max dx | max dy | frames within 1 px | r (y) |
|---|---|---|---|---|---|---|
| moving | plain | 200/200 | 0 | 0 | 200/200 | 1.000000 |
| moving | cam | 200/200 | 0 | 0 | 200/200 | 1.000000 |
| moving | cutmem | 200/200 | 0 | 0 | 200/200 | 1.000000 |
| moving | all | 200/200 | 0 | 0 | 200/200 | 1.000000 |
| long | plain | 7500/7500 | 0 | 0 | 7500/7500 | 1.000000 |
| long | cam | 7500/7500 | 0 | 0 | 7500/7500 | 1.000000 |
| long | cutmem | 7500/7500 | 0 | 0 | 7500/7500 | 1.000000 |
| long | all | 7500/7500 | 0 | 0 | 7500/7500 | 1.000000 |
| panning | plain | 250/250 | 0 | 0 | 250/250 | 1.000000 |
| panning | cam | 250/250 | 0 | 0 | 250/250 | 1.000000 |
| panning | cutmem | 250/250 | 0 | 0 | 250/250 | 1.000000 |
| panning | all | 250/250 | 0 | 0 | 250/250 | 1.000000 |
| cut | plain | 200/200 | 0 | 0 | 200/200 | 1.000000 |
| cut | cam | 200/200 | 0 | 0 | 200/200 | 1.000000 |
| cut | cutmem | 200/200 | 0 | 0 | 200/200 | 1.000000 |
| cut | all | 200/200 | 0 | 0 | 200/200 | 1.000000 |

## What this does and does not show
- The tracking loop, scene cuts, camera compensation (LK + RANSAC, then savgol per scene), the progress clock and the axis choice match 0.7.
- The synthetic clips never make CSRT lose the box (lost = 0 on every run). So the parity runs do not exercise the paths after a loss: jump guard, coast and reacquire. Unit tests cover these instead, ported from 0.7's `dispguard_test.go`, `appearance_memory_test.go` and `coast_test.go`, plus a lighting-cut clip with a jump that only reacquire can follow (`tests/engine/track_box_test.cpp`).
- **Deliberate difference:** times are the decoder's video time (GD-07), not 0.7's frame number × 1000 / fps. On these constant-rate clips both clocks agree.
- **Finding for P3.9:** with 0.7's loose box, the memory practically never reacquires after a cut to another scene (`LESSONS.md` GD-10).

## Repeat
```
cmake --build build --target engine_parity_dump && build/tests/engine_parity_dump <folder>
```
Then run 0.7's `TrackROI` on `<folder>/*.mp4` with the same options (`go test -tags opencv` in `generator/trackcv`) and compare the CSVs.
