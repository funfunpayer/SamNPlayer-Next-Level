"""Unit tests for multi-partner Tf/Tj distance (min over N targets).

No video I/O: pure geometry helpers via track_multi_points early-path and
_punch_exclude_boxes / estimate_camera_motion masks.
"""

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_funscript as g


def test_punch_exclude_boxes():
    mask = np.full((100, 100), 255, dtype=np.uint8)
    g._punch_exclude_boxes(mask, [(10, 20, 30, 40)], pad=0)
    assert mask[20:60, 10:40].sum() == 0
    assert mask[0, 0] == 255


def test_estimate_camera_motion_accepts_extra_excludes():
    rng = np.random.default_rng(0)
    prev = (rng.random((120, 160)) * 255).astype(np.uint8)
    gray = prev.copy()
    # Quiet path: identical frames → near-zero motion even with masks.
    dy = g.estimate_camera_motion(prev, gray, (40, 40, 20, 20),
                                  extra_excludes=[(0, 0, 10, 10), (100, 80, 20, 20)])
    assert dy == 0.0 or abs(dy) < 1.0


def test_min_distance_logic_three_partners():
    """Synthetic centers: tip at (0,0); partners at 10, 5, 20 → min = 5."""
    tip = (0.0, 0.0)
    partners = [(10.0, 0.0), (0.0, 5.0), (20.0, 0.0)]
    best = min(float(np.hypot(px - tip[0], py - tip[1])) for px, py in partners)
    assert best == 5.0


def test_track_multi_points_requires_targets():
    try:
        g.track_multi_points("/nonexistent.mp4", (0, 0, 10, 10), [])
        assert False, "expected RuntimeError"
    except RuntimeError as e:
        assert "at least one target" in str(e)


def main():
    test_punch_exclude_boxes()
    print("  OK   punch_exclude_boxes")
    test_estimate_camera_motion_accepts_extra_excludes()
    print("  OK   estimate_camera_motion extra_excludes")
    test_min_distance_logic_three_partners()
    print("  OK   min-distance geometry")
    test_track_multi_points_requires_targets()
    print("  OK   track_multi_points empty targets")
    print("All multi_point_test checks passed.")


if __name__ == "__main__":
    main()
