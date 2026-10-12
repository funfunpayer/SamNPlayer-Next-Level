"""Unit tests for tracking_gaps_from_flags (no video / OpenCV needed beyond import)."""

import generate_funscript as g


def test_empty():
    assert g.tracking_gaps_from_flags([], []) == []
    assert g.tracking_gaps_from_flags([0, 40, 80], [False, False, False]) == []


def test_single_block():
    ts = [0, 40, 80, 120, 160]
    lost = [False, True, True, False, False]
    gaps = g.tracking_gaps_from_flags(ts, lost, merge_gap_ms=100)
    assert gaps == [{"start_ms": 40, "end_ms": 80}]


def test_merge_close_blocks():
    ts = [0, 40, 80, 120, 160, 200]
    lost = [True, True, False, True, True, False]
    # 80→120 is 40ms < 100 merge → one gap
    gaps = g.tracking_gaps_from_flags(ts, lost, merge_gap_ms=100)
    assert len(gaps) == 1
    assert gaps[0]["start_ms"] == 0
    assert gaps[0]["end_ms"] == 160


def test_separate_blocks():
    ts = [0, 50, 100, 300, 350, 400]
    lost = [True, True, False, True, True, False]
    gaps = g.tracking_gaps_from_flags(ts, lost, merge_gap_ms=100)
    assert len(gaps) == 2
    assert gaps[0] == {"start_ms": 0, "end_ms": 50}
    assert gaps[1] == {"start_ms": 300, "end_ms": 350}


if __name__ == "__main__":
    test_empty()
    test_single_block()
    test_merge_close_blocks()
    test_separate_blocks()
    print("ok")
