"""Parity tests for native funscript parse/write vs funscript/funscript.go.

Mirrors key cases from funscript_test.go and parse_compat_test.go (core path).
Also covers ShiftActions from shift.go.
"""

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "build"))

try:
    from samn_native import funscript as fs
except ImportError as e:
    print("SKIP: samn_native not built", file=sys.stderr)
    print(e, file=sys.stderr)
    # CI sets SAMN_NATIVE_REQUIRED=1: a missing module must fail there, not pass.
    sys.exit(1 if os.environ.get("SAMN_NATIVE_REQUIRED") else 0)


def pairs(s):
    return [(a.at, a.pos) for a in s.actions]


def test_float_actions():
    s = fs.parse('{"actions":[{"at":500.4,"pos":99.6},{"at":0.0,"pos":0.5},{"at":1000,"pos":50}]}')
    assert pairs(s) == [(0, 1), (500, 100), (1000, 50)]
    # write uses integers
    out = fs.write(s)
    assert '"at": 500' in out or '"at":500' in out
    assert '"pos": 100' in out or '"pos":100' in out


def test_corrupt_times():
    s = fs.parse('{"actions":[{"at":-922337203565470,"pos":0},{"at":-922337203565470,"pos":99},'
                 '{"at":0,"pos":10},{"at":500,"pos":90},{"at":9999999999999,"pos":50}]}')
    assert pairs(s) == [(0, 10), (500, 90)]
    assert s.dropped == 3
    try:
        fs.parse('{"actions":[{"at":-5,"pos":0}]}')
        assert False, "expected error"
    except RuntimeError:
        pass


def test_duration_ofs():
    cases = [
        ('{"actions":[{"at":0,"pos":0},{"at":631000,"pos":100}],"metadata":{"duration":631.8}}', 631800),
        ('{"actions":[{"at":0,"pos":0},{"at":631000,"pos":100}],"metadata":{"duration":631}}', 631000),
        ('{"actions":[{"at":0,"pos":0},{"at":631000,"pos":100}],"metadata":{"duration":631800}}', 631800),
        ('{"actions":[{"at":0,"pos":0},{"at":1000,"pos":100}],"metadata":{"duration":"10:31"}}', 631000),
        ('{"actions":[{"at":0,"pos":0},{"at":1000,"pos":100}],"metadata":{"duration":"soon"}}', 0),
        ('{"actions":[{"at":0,"pos":0},{"at":1000,"pos":100}],"metadata":{"duration":null}}', 0),
    ]
    for js, want in cases:
        s = fs.parse(js)
        assert s.duration_meta == want, (js, s.duration_meta, want)


def test_inverted():
    s = fs.parse('{"actions":[{"at":0,"pos":10},{"at":1000,"pos":90}],"inverted":true}')
    assert s.inverted
    assert pairs(s) == [(0, 10), (1000, 90)]


def test_clamp_pos():
    s = fs.parse('{"actions":[{"at":0,"pos":-5},{"at":100,"pos":150}]}')
    assert pairs(s) == [(0, 0), (100, 100)]


def test_shift_actions():
    acts = [fs.Action(0, 10), fs.Action(100, 90), fs.Action(300, 20)]
    later, d = fs.shift_actions(acts, 250)
    assert d == 0
    assert [(a.at, a.pos) for a in later] == [(250, 10), (350, 90), (550, 20)]
    earlier, d = fs.shift_actions(acts, -150)
    assert d == 2
    assert len(earlier) == 1
    assert earlier[0].at == 150
    # input unmodified
    assert acts[0].at == 0


def main():
    test_float_actions()
    test_corrupt_times()
    test_duration_ofs()
    test_inverted()
    test_clamp_pos()
    test_shift_actions()
    print("All funscript parity checks passed.")


if __name__ == "__main__":
    main()
