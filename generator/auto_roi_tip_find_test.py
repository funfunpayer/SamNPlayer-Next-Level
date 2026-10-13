"""Tip-Find quality vs loose threshold bbox (Everyday Create).

Background: classic find_roi took the bounding box of *all* cells ≥ 0.5×max.
On diffuse residual motion that spanned most of the frame — measured as a
bad tip seed (TFTJ: CSRT auto_roi huge box). Everyday Tip-Find now picks a
compact _peak_regions winner with a tip compactness prior.

This test:
  1. Unit-checks tip scoring prefers a compact peak over a huge bbox region.
  2. On a synthetic score grid (one tip peak + near-threshold haze), compares
     legacy threshold bbox area vs peak Tip-Find box — peak must stay compact
     and cover the tip cell.
  3. Smoke: find_roi on the two_point synthetic video returns a box that
     covers less than half the frame.

Ausführen: python3 generator/auto_roi_tip_find_test.py
"""

import sys
import tempfile
from pathlib import Path

import numpy as np

import auto_roi
from two_point_test import write_video, W, H


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    rows, cols = 8, 12
    # --- 1) tip score: compact peak beats near-full-frame blob --------------
    scores = np.zeros((rows, cols))
    scores[2, 3] = 1.0
    scores[2, 4] = 0.85
    scores[3, 3] = 0.80
    compact = [(2, 3), (2, 4), (3, 3)]
    huge = [(r, c) for r in range(rows) for c in range(cols) if scores[r, c] >= 0.0]
    # Raise haze so "huge" has the same peak but covers the whole grid.
    scores_haze = scores.copy()
    scores_haze[scores_haze == 0] = 0.55
    scores_haze[2, 3] = 1.0
    tip_compact = auto_roi._tip_region_score(scores_haze, compact, rows, cols)
    tip_huge = auto_roi._tip_region_score(scores_haze, huge, rows, cols)
    check("tip score prefers compact peak over full-grid haze",
          tip_compact > tip_huge, f"compact={tip_compact:.3f} huge={tip_huge:.3f}")

    # --- 2) peak pick vs legacy threshold bbox on haze grid -----------------
    peak_regions = auto_roi._peak_regions(scores_haze, max_regions=3)
    check("peak regions found", len(peak_regions) >= 1, str(len(peak_regions)))
    best = max(peak_regions,
               key=lambda cells: auto_roi._tip_region_score(scores_haze, cells, rows, cols))
    check("tip cell in winning peak region", (2, 3) in best, str(best))
    brs = [r for r, c in best]
    bcs = [c for r, c in best]
    peak_area = (max(brs) - min(brs) + 1) * (max(bcs) - min(bcs) + 1)
    # Legacy: all cells ≥ 0.5×max → almost the whole haze grid.
    thr = scores_haze.max() * 0.5
    legacy_mask = scores_haze >= thr
    legacy_area = int(legacy_mask.sum())  # cell count above threshold
    # Bounding-box area of legacy mask (what old find_roi used):
    lr, lc = np.where(legacy_mask)
    legacy_bbox = (lr.max() - lr.min() + 1) * (lc.max() - lc.min() + 1)
    check("peak tip region smaller than legacy threshold bbox",
          peak_area < legacy_bbox * 0.5,
          f"peak={peak_area} legacy_bbox={legacy_bbox} cells_above={legacy_area}")
    check("peak tip region under 25% of grid",
          peak_area / (rows * cols) < 0.25,
          f"{peak_area}/{rows * cols}")

    # Geometry path with realistic video size (min_side=24 needs room).
    width, height = 320, 240
    scale = 320.0 / width
    cell_w = (width * scale) / cols
    cell_h = (height * scale) / rows
    # Inline legacy threshold bbox (deleted helper — was Ausfallcode only for
    # comparison). Same formula: bbox of all cells ≥ 0.5×max.
    lx0 = int(lc.min() * cell_w / scale)
    lx1 = int((lc.max() + 1) * cell_w / scale)
    ly0 = int(lr.min() * cell_h / scale)
    ly1 = int((lr.max() + 1) * cell_h / scale)
    legacy_box = auto_roi._shrink_box(lx0, ly0, lx1 - lx0, ly1 - ly0, width, height)
    peak_xywh = auto_roi._cells_to_box(best, cell_w, cell_h, scale, width, height)
    peak_box = auto_roi._shrink_box(*peak_xywh, width, height)
    legacy_frac = (legacy_box[2] * legacy_box[3]) / (width * height)
    peak_frac = (peak_box[2] * peak_box[3]) / (width * height)
    check("legacy threshold box covers majority of frame",
          legacy_frac > 0.5, f"legacy_frac={legacy_frac:.2f} box={legacy_box}")
    check("peak tip box under half the frame",
          peak_frac < 0.5, f"peak_frac={peak_frac:.2f} box={peak_box}")
    check("peak tip box smaller than legacy",
          peak_frac < legacy_frac * 0.6,
          f"peak={peak_frac:.2f} legacy={legacy_frac:.2f}")

    # --- 3) end-to-end find_roi stays compact on synthetic motion video -----
    with tempfile.TemporaryDirectory() as tmp:
        video = Path(tmp) / "motion.mp4"
        write_video(video)
        x, y, w, h = auto_roi.find_roi(
            str(video), max_seconds=5, report_progress=False)
        frac = (w * h) / float(W * H)
        check("find_roi returns positive box", w >= 16 and h >= 16, f"{x},{y},{w},{h}")
        check("find_roi box under half the frame (vs manual-ish tip seed)",
              frac < 0.50, f"frac={frac:.2f} box={x},{y},{w},{h}")
        # Candidates ranking must agree: top candidate tip score ≥ others.
        cands = auto_roi.find_roi_candidates(
            str(video), max_seconds=5, report_progress=False)
        check("candidates non-empty", len(cands) >= 1, str(len(cands)))
        check("candidates ranked by tip score",
              all(cands[i]["score"] >= cands[i + 1]["score"]
                  for i in range(len(cands) - 1)),
              str([c["score"] for c in cands]))

    print(("FAILED: " + ", ".join(failures)) if failures else "All checks passed.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
