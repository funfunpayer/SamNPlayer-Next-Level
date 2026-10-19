# P3: Find tip (auto ROI) against 0.7

**What:** `samn::engine::FindTip` (engine/src/tip_find.cpp) is the C++ port of
0.7's `generator/auto_roi.py` (`find_roi`, `find_roi_candidates`), which 0.7 ran
as a Python process. Same steps: 12 x 8 grid at 320 px, every second frame,
Farneback flow minus the camera's own motion, periodicity (FFT, 0.1-4 Hz) x
motion size per cell, peak regions (decay 0.4), tip score with the soft
compactness knee at 12 %, a 10 % inset.

**How measured (19 Oct 2026):** the engine test writes its clips with
`SAMN_TIP_KEEP_DIR`; 0.7's `auto_roi.py` ran on the same files with Python 3.12
and OpenCV 4.6.0 (the library the C++ build uses), the C++ side with a small
dump program.

| Clip | 0.7 box | C++ box | Candidates (box, score) |
|---|---|---|---|
| disc at 1 Hz on a busy picture, 8 s | (116, 96, 86, 48) | (116, 96, 86, 48) | all 6 equal, scores to 4 decimals (best 3.9106) |
| same with the camera panning, 10 s | (116, 96, 86, 48) | (116, 96, 86, 48) | all 6 equal (best 1.8188) |
| still disc (only compression noise), 4 s | (82, 183, 22, 24) | (82, 183, 22, 24) | all 6 equal (best 0.0016) |

**Result:** bit-for-bit the same proposals. The still clip shows 0.7's
behaviour on a video without rhythm: a tiny box with a score near zero
instead of an error; the Generator page reports such a find as weak.
