# Label tracking memory, 8 Nov 2026

`TrackBetweenKeys` (tracking between two label keyframes) before and after the bounded frame window.

**Setup:** `samn_label_mem` (`engine/tools/samn_label_mem.cpp`), Linux, Release, 4 cores shared with other jobs (wall time is noisy). Synthetic clip, made by the tool: 1280x720, 25 fps, 40 s = 1000 frames (mp4v, a bright disc on a texture; no media). Keys at 0 and 39.9 s, step 200 ms, tracked at 640x360. Each run is its own process; peak RSS is `VmHWM` from `/proc/self/status`.

```
cmake -S . -B build -DSAMN_BUILD_APP=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --target samn_label_mem
build/engine/samn_label_mem --make clip.mp4 40
build/engine/samn_label_mem --track clip.mp4 40
```

| Run (interleaved, 3 each) | Peak RSS | Wall ms/frame | CPU s (user+sys) | Output hash |
| --- | --- | --- | --- | --- |
| Before, hold every frame | 758 MB | 43.5 / 46.8 / 41.5 (mean 43.9) | 63.6 / 57.6 / 57.1 (mean 59.4) | a37fb50b672fb1a7 |
| After, window 24, prefetch | 135 MB | 51.7 / 47.2 / 38.1 (mean 45.7) | 58.5 / 60.4 / 61.6 (mean 60.2) | a37fb50b672fb1a7 |
| Change | **5.6x less** | +4 % (spread of one binary is about 20 %) | +1 % | identical |

An earlier after build without the prefetch thread (window 32, serial re-decode) measured 120 MB and +12 % CPU: decoding the stretch twice costs real CPU, so the next chunk is now decoded on a second thread while the current one is tracked. CPU time stays about the same; wall time is hidden where a core is free.

**Output:** the tracked boxes are bit-identical (hash over the exact bits of every sample, and the engine test compares with a copy of the old hold-everything algorithm for windows of 1, 7, 24 and 500 frames).

**Floor:** about 100 MB of the 135 MB is the decoder, the tracker and the process itself; the frames are 33 MB (two windows).

**Not measured:** Windows, real clips (long GOP: each backward seek decodes from the previous key frame).
