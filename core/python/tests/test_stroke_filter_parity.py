"""Parity tests for native stroke filter vs funscript/stroke_filter.go.

Mirrors key cases from stroke_filter_test.go.
"""

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "build"))

try:
    from samn_native import stroke_filter as sf
except ImportError as e:
    print("SKIP: samn_native not built", file=sys.stderr)
    print(e, file=sys.stderr)
    # CI sets SAMN_NATIVE_REQUIRED=1: a missing module must fail there, not pass.
    sys.exit(1 if os.environ.get("SAMN_NATIVE_REQUIRED") else 0)


def actions_from_pairs(pairs):
    return [sf.Action(at, pos) for at, pos in pairs]


def pairs_from_actions(acts):
    return [(a.at, a.pos) for a in acts]


def test_off_changes_nothing():
    zigzag = actions_from_pairs([(0, 0), (500, 100), (1000, 0), (1500, 100), (2000, 0)])
    f = sf.StrokeFilter()
    out = sf.apply_stroke_filter(zigzag, f)
    assert pairs_from_actions(out) == pairs_from_actions(zigzag)


def test_smooth_keeps_points():
    zigzag = actions_from_pairs([(0, 0), (500, 100), (1000, 0), (1500, 100), (2000, 0)])
    for mode in ("pchip", "makima"):
        f = sf.StrokeFilter()
        f.smooth = mode
        f.stepMs = 50
        out = sf.apply_stroke_filter(zigzag, f)
        assert len(out) > len(zigzag)
        for a in out:
            assert 0 <= a.pos <= 100
        times = [a.at for a in out]
        assert times == sorted(times) and len(set(times)) == len(times)
        # original points present
        orig = set(pairs_from_actions(zigzag))
        got = set(pairs_from_actions(out))
        assert orig.issubset(got)


def test_speed_limit():
    acts = actions_from_pairs([(0, 0), (100, 100), (1000, 100)])
    f = sf.StrokeFilter()
    f.maxSpeed = 400
    out = sf.apply_stroke_filter(acts, f)
    assert out[1].pos == 40  # 400 u/s * 0.1 s


def test_idle_fill():
    acts = actions_from_pairs([(0, 50), (10000, 50)])
    f = sf.StrokeFilter()
    f.idleFillMs = 4000
    out = sf.apply_stroke_filter(acts, f)
    assert len(out) > 2
    for a in out:
        assert abs(a.pos - 50) <= 12


def main():
    test_off_changes_nothing()
    test_smooth_keeps_points()
    test_speed_limit()
    test_idle_fill()
    print("All stroke filter parity checks passed.")


if __name__ == "__main__":
    main()
