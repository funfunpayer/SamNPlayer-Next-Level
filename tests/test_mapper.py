"""Port of SamNPlayer funscript/mapper_test.go (same inputs and tolerances)."""
import math

import pytest

from samn.formats.mapper import (MapOptions, SyncMode, default_map_options,
                                 to_intensity_curve)


def test_standstill():
    actions = [(0, 50), (1000, 50), (2000, 50)]
    opts = default_map_options()
    opts.min_vibration = 0
    opts.smoothing = 0
    frames = to_intensity_curve(actions, opts)
    assert frames, "keine Frames erzeugt"
    for f in frames:
        assert f.vibration <= 0.001, f"Stillstand ergibt Vibration {f.vibration:.3f} bei {f.at}ms"


def test_fast_stroke_reaches_full():
    actions = [(0, 0), (150, 100), (300, 0)]
    opts = default_map_options()
    opts.smoothing = 0
    frames = to_intensity_curve(actions, opts)
    assert max(f.vibration for f in frames) >= 0.99


def test_floor_preserves_gradations():
    opts = default_map_options()
    opts.min_vibration = 0.15
    opts.smoothing = 0
    opts.sync = SyncMode.VIBRATION_ONLY

    def intensity(delta_pos: int, delta_ms: int) -> float:
        frames = to_intensity_curve([(0, 0), (delta_ms, delta_pos), (delta_ms * 2, 0)], opts)
        assert frames, "keine Frames"
        return frames[0].vibration

    still = intensity(0, 200)
    slow = intensity(12, 200)
    medium = intensity(24, 200)

    assert abs(still - 0.15) <= 0.01
    assert abs(slow - 0.235) <= 0.02
    assert abs(medium - 0.32) <= 0.02
    assert slow > still + 0.01
    assert medium > slow + 0.01


def test_floor_keeps_ceiling():
    opts = default_map_options()
    opts.min_vibration = 0.4
    opts.smoothing = 0
    opts.sync = SyncMode.VIBRATION_ONLY
    for f in to_intensity_curve([(0, 0), (100, 100), (200, 0)], opts):
        assert f.vibration <= 1.0001


def test_floor_respects_channel_modes():
    actions = [(0, 0), (200, 100), (400, 0)]
    opts = default_map_options()
    opts.min_vibration = 0.3
    opts.min_suction = 0.3
    opts.smoothing = 0

    opts.sync = SyncMode.VIBRATION_ONLY
    for f in to_intensity_curve(actions, opts):
        assert f.suction <= 0.001

    opts.sync = SyncMode.SUCTION_ONLY
    for f in to_intensity_curve(actions, opts):
        assert f.vibration <= 0.001


def test_independent_uses_position_for_suction():
    actions = [(0, 0), (2000, 100)]
    opts = default_map_options()
    opts.smoothing = 0
    opts.min_vibration = 0
    opts.min_suction = 0
    opts.sync = SyncMode.INDEPENDENT

    frames = to_intensity_curve(actions, opts)
    assert len(frames) >= 10
    first, last = frames[0], frames[-2]
    assert abs(first.vibration - last.vibration) <= 0.05
    assert last.suction > first.suction + 0.5


def test_smoothing_stays_in_range():
    actions = [(0, 0), (100, 100), (200, 0), (300, 100)]
    opts = default_map_options()
    opts.smoothing = 0.8
    for f in to_intensity_curve(actions, opts):
        assert 0 <= f.vibration <= 1 and 0 <= f.suction <= 1

    opts.smoothing = 0
    raw_max = max(f.vibration for f in to_intensity_curve(actions, opts))
    opts.smoothing = 0.8
    smooth_max = max(f.vibration for f in to_intensity_curve(actions, opts))
    assert smooth_max <= raw_max + 0.001


def test_edge_cases():
    opts = default_map_options()
    assert to_intensity_curve([], opts) is None
    assert to_intensity_curve([(0, 50)], opts) is None

    duplicate = [(0, 0), (0, 100), (500, 0)]
    for f in to_intensity_curve(duplicate, opts):
        assert not (math.isnan(f.vibration) or math.isinf(f.vibration))

    broken = default_map_options()
    broken.tick_ms = 0
    broken.max_speed = 0
    frames = to_intensity_curve([(0, 0), (1000, 100)], broken)
    assert frames
    for f in frames:
        assert not math.isnan(f.vibration)


def test_frame_timeline():
    actions = [(0, 0), (1000, 100), (2000, 0)]
    opts = default_map_options()
    opts.tick_ms = 50
    frames = to_intensity_curve(actions, opts)
    assert len(frames) >= 40
    assert frames[0].at == 0
    for i in range(1, len(frames)):
        assert frames[i].at > frames[i - 1].at
    assert frames[-1].at >= 1950
