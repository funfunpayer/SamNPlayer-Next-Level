"""Parity tests for native/ beatbar vs generator/beatbar.go.

Mirrors the synthetic cases in generator/beatbar_test.go (TestFindBeatBarAndBeats
and the plain-noise negative case). Requires the beatbar_native module to be
built and importable (see native/README.md).

Run from repo root after building:
  PYTHONPATH=native/build python3 native/tests/test_beatbar_parity.py
"""

import math
import random
import os
import sys
from pathlib import Path

# Allow import from build dir or installed location
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "build"))

try:
    import beatbar_native as bb
except ImportError as e:
    print("SKIP: beatbar_native not built or not on PYTHONPATH", file=sys.stderr)
    print(e, file=sys.stderr)
    # CI sets SAMN_NATIVE_REQUIRED=1: a missing module must fail there, not pass.
    sys.exit(1 if os.environ.get("SAMN_NATIVE_REQUIRED") else 0)


def synth_beat_video(n, w, h, y0, y1, target_x, speed, period, seed):
    """Port of synthBeatVideo from generator/beatbar_test.go."""
    rng = random.Random(seed)
    frames = []
    dots = []  # list of x (float)
    beats = []
    start = float(w - 4) if speed < 0 else 3.0

    for i in range(n):
        pix = bytearray(w * h)
        for k in range(w * h):
            pix[k] = rng.randint(0, 119)

        if i % period == 0:
            dots.append(start)
            # true beat time in frames
            beats.append(float(i) + abs(start - float(target_x)) / abs(speed))

        # dark strip background + target line
        for y in range(y0, y1):
            for x in range(w):
                pix[y * w + x] = 10
            pix[y * w + target_x] = 200

        # moving dots
        keep = []
        for dx in dots:
            passed = (speed < 0 and dx < target_x) or (speed > 0 and dx > target_x)
            if passed:
                continue
            cx = int(round(dx))
            for y in range(y0, y1):
                for x in range(cx - 2, cx + 3):
                    if 0 <= x < w:
                        pix[y * w + x] = 250
            keep.append(dx + speed)
        dots = keep

        frame = bb.GrayFrame()
        frame.w = w
        frame.h = h
        frame.pix = list(pix)  # pybind expects list or bytes; list is fine
        frames.append(frame)

    real_beats = [b for b in beats if b < float(n - 1)]
    return frames, real_beats


def check_case(name, speed, target_x):
    print(f"=== {name}")
    w, h = 320, 192
    frames, truth = synth_beat_video(300, w, h, 168, 176, target_x, speed, 17, 1)

    result = bb.find_beat_bar(frames)
    if result is None:
        raise AssertionError(f"{name}: strip not found")
    bar = result

    if bar.y0 != 168 or bar.y1 != 176:
        raise AssertionError(f"{name}: wrong band {bar.y0}-{bar.y1}")
    if abs(bar.velocity - speed) > 0.6:
        raise AssertionError(f"{name}: velocity {bar.velocity}, want {speed}")
    if abs(bar.target_x - float(target_x)) > 4:
        raise AssertionError(f"{name}: target {bar.target_x}, want {target_x}")

    beats_ms = bb.beat_times(frames, bar, 30.0)
    frame_ms = 1000.0 / 30.0

    if not (len(truth) - 1 <= len(beats_ms) <= len(truth) + 1):
        raise AssertionError(f"{name}: {len(beats_ms)} beats, want ~{len(truth)}")

    for tb in truth:
        best = min(abs(b - tb * frame_ms) for b in beats_ms) if beats_ms else float("inf")
        if best > 1.5 * frame_ms:
            raise AssertionError(f"{name}: beat at {tb * frame_ms:.0f} ms missed (off by {best:.0f})")

    print(f"  OK  band={bar.y0}-{bar.y1} vel={bar.velocity:.2f} target={bar.target_x:.1f} "
          f"score={bar.score:.2f} beats={len(beats_ms)}")


def test_noise_rejected():
    print("=== plain noise should not find a bar")
    rng = random.Random(2)
    frames = []
    for _ in range(120):
        pix = bytearray(160 * 96)
        for k in range(160 * 96):
            pix[k] = rng.randint(0, 199)
        f = bb.GrayFrame()
        f.w = 160
        f.h = 96
        f.pix = list(pix)
        frames.append(f)

    result = bb.find_beat_bar(frames)
    if result is not None:
        raise AssertionError(f"noise taken for a beat bar: y0={result.y0} score={result.score}")
    print("  OK  no bar found")


def main():
    check_case("leftward to the left third", -8.0, 60)
    check_case("rightward to the middle", 6.0, 160)
    test_noise_rejected()
    print("\nAll parity checks passed.")


if __name__ == "__main__":
    main()
