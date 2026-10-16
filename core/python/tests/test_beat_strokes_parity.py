"""Parity tests for native BeatStrokes vs funscript/beat_strokes.go.

Mirrors cases from beat_strokes_test.go.
"""

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "build"))

try:
    import beat_strokes_native as bs
except ImportError as e:
    print("SKIP: beat_strokes_native not built", file=sys.stderr)
    print(e, file=sys.stderr)
    # CI sets SAMN_NATIVE_REQUIRED=1: a missing module must fail there, not pass.
    sys.exit(1 if os.environ.get("SAMN_NATIVE_REQUIRED") else 0)


def pairs_from_actions(acts):
    return [(a.at, a.pos) for a in acts]


def test_down_on_every_beat():
    beats = [1000.0, 1500.0, 2000.0, 2500.0]
    out = bs.beat_strokes(beats, bs.BeatStrokeOptions())
    bottoms = {a.at for a in out if a.pos == 10}
    for b in beats:
        assert int(b) in bottoms, f"no bottom at beat {b}: {pairs_from_actions(out)}"
    # 250 ms up at 500/s = 125 units, capped at Top 90
    for a in out:
        if a.at == 1250:
            assert a.pos == 90, f"full stroke expected: {a}"


def test_speed_limit_and_hold():
    # Fast beats (100 ms apart) get short strokes; a 5 s pause holds.
    opts = bs.BeatStrokeOptions()
    opts.maxSpeed = 400
    out = bs.beat_strokes([0.0, 100.0, 200.0, 5200.0], opts)
    for i in range(1, len(out)):
        dt = (out[i].at - out[i - 1].at) / 1000.0
        diff = abs(out[i].pos - out[i - 1].pos)
        assert diff <= 400 * dt + 1, f"too fast between {out[i-1]} and {out[i]}"
        assert out[i].at > out[i - 1].at
    held = sum(1 for a in out if 200 < a.at < 5200)
    assert held == 2, f"pause should rise once and hold (2 points), got {held}: {pairs_from_actions(out)}"
    assert len(bs.beat_strokes([], bs.BeatStrokeOptions())) == 0


def main():
    test_down_on_every_beat()
    test_speed_limit_and_hold()
    print("All beat strokes parity checks passed.")


if __name__ == "__main__":
    main()
