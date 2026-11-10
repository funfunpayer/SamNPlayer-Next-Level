# Label tracking memory, 8 Nov 2026

`TrackBetweenKeys` (tracking between two label keyframes) before and after the bounded frame window.

**Setup:** `samn_label_mem` (`engine/tools/samn_label_mem.cpp`), Linux, Release, 4 cores shared with other jobs (wall time is very noisy, 55 to 155 s for the same binary; CPU time is steadier and is the number to read). Synthetic clips only, made by the tool and ffmpeg, nothing stored: 1280x720 (and a 1920x1080 scale-up), 25 fps, 40 s = 1000 frames, a bright disc on a texture. Keys at 0 and 39.9 s, step 200 ms, tracked at 640 px wide (a small frame is 0.69 MB whatever the source size). Each run is its own process; peak RSS is `VmHWM` from `/proc/self/status`. Before = origin/main (every frame held); after = this branch. Two interleaved runs each.

```
build/engine/samn_label_mem --make cfr720.mp4 40
ffmpeg -i cfr720.mp4 -c:v libx264 -preset veryfast -g 250 -bf 3 -keyint_min 250 -sc_threshold 0 gop250_720.mp4
ffmpeg -i cfr720.mp4 -vf scale=1920:1080 -c:v libx264 -preset veryfast -g 250 -bf 3 -keyint_min 250 -sc_threshold 0 gop250_1080.mp4
build/engine/samn_label_mem --track cfr720.mp4 40          # 1080 clip: ... gop250_1080.mp4 40 1.5
```

| Clip | | Peak RSS | CPU s (user+sys), 2 runs, mean | Output hash |
| --- | --- | --- | --- | --- |
| 720p mp4v, GOP 12 | before | 758 MB | 69.3 / 69.6 (69.4) | a37fb50b672fb1a7 |
| | after | 137 MB (window 25) | 67.0 / 68.8 (67.9) | a37fb50b672fb1a7 |
| | change | **5.5x less** | -2 % | identical |
| 720p H.264, GOP 250, 3 B-frames | before | 769 MB | 64.5 / 67.8 (66.1) | 40bd48ab98bbaadb |
| | after | 243 MB (window 96) | 69.0 / 70.1 (69.6) | 40bd48ab98bbaadb |
| | change | 3.2x less | +5 % | identical |
| 1080p H.264, GOP 250, 3 B-frames | before | 802 MB | 74.7 / 76.8 (75.7) | 2c4f5b98fda70ad1 |
| | after | 281 MB (window 96) | 88.1 / 91.3 (89.7) | 2c4f5b98fda70ad1 |
| | change | 2.9x less | **+18 %** | identical |

Wall ms per frame (same runs, noisy): GOP 12 before 62 / 94, after 60 / 85; GOP 250 720p before 57 / 91, after 69 / 106; GOP 250 1080p before 77 / 116, after 103 / 156 (the machine was loaded by other jobs in the second run of each pair).

**Acceptance.** Short GOP (the synthetic clip, GOP 12): RSS 5.5x down, CPU -2 %, output identical: met. Long GOP: not met and reported as measured. The 5x RSS cut and +10 % CPU cannot both hold there: every backward chunk is a seek that runs through up to a GOP of frames, and the backward pass decodes the whole stretch a second time (about 8 ms per 1080p frame, 1000 frames, about 12 % of the 75 s of tracking, plus the seeks). A larger window cuts the seek cost but costs RSS: the window grows from 24 to at most 96 frames (two windows, 133 MB of frames, about 280 MB RSS at most) when the measured seek time says so; that is the chosen limit ("under about 300 MB").

**How the window is chosen.** The boxes were equal to the old algorithm's for every window tried (1, 3, 7, 24, 500 on CFR; 1, 3, 24 on cut, VFR, long-GOP and equal-timestamp clips), so the window is a memory and speed setting, not a quality one; that is tested on those clips, not proved for every stream. The window starts at 24. From the second chunk on, the mean time of a seek (to the first frame of a chunk, in the decode thread) is compared with 5 % of the time to track one frame (measured in the forward run): on the GOP 12 clip a seek is about 30 ms and a tracked frame about 30 ms, so the window stays near 24 (25 to 26 seen); on GOP 250 a seek is 300 to 900 ms and the window goes to the cap of 96. OpenCV's seek costs far more than the frames it decodes (a grab is 0.5 ms, a seek 20 to 50 ms even on GOP 12), so the GOP cannot be read off the number of frames.

**Not a count of decoded frames.** `stats.frames_decoded` (1997 here) counts frames grabbed from the file including the warm-up after a seek that the tool sees; frames the decoder runs through inside the seek are not counted. Read CPU time for the real cost.

**Variable frame rate.** A seek lands by the nominal frame rate, which on a VFR stream can be after the frame wanted. The backward decode finds its frame by time: a seek to the wanted time, grab (decode only, no picture) until the frame time is reached, and if the seek landed after it, retry from 2 s, 8 s, ... earlier and at last from the first key's own position. The engine test makes VFR clips (irregular drops, every third frame dropped) and a long-GOP clip with ffmpeg and compares the boxes with a copy of the old algorithm for windows of 1, 3 and 24: identical.

**Equal timestamps.** Frames sharing a millisecond: the wanted frame can be the k-th of a run, a seek can land inside a run, and OpenCV can drop frames of the run a seek lands in, so the stream after a seek is not always what the forward run read. The decode seeks first to the time before the run, accepts a landing only after an earlier frame, and steps over the earlier frames of the run; the last resort, and a chunk that starts in the first run, decodes on a fresh reader opened like the forward run's. Tested, boxes identical to the old algorithm and decode count bounded: ffmpeg-made Matroska clips (mpeg4 and mjpeg, GOP 7 and default, pairs and triples of equal timestamps, one with a start at 1.3 s), 4 s stretches, keys at the start, at a run start and between runs, windows 1, 3, 24 and the adaptive default. Not tested: other codecs, longer runs, real recordings.

**Output.** Bit-identical boxes (hash over the exact bits of every sample, and the engine tests compare with the old hold-everything algorithm: CFR, cut clip with both runs losing the track, VFR, long GOP).

**Floor.** About 100 MB of the RSS is the decoder, the tracker and the process; the frames are the rest.

**Not measured:** Windows, real clips (phones: VFR, GOP 30 to 60 typically, B-frames; the numbers above are synthetic patterns).
