"""Tests for support_signals.py — synthetic frames only."""

import sys

import numpy as np

import support_signals


def check(label, ok, detail=""):
    if not ok:
        print(f"FAIL: {label}" + (f" ({detail})" if detail else ""))
        return label
    print(f"OK: {label}")
    return None


def main():
    failures = []

    gray = np.zeros((120, 160), dtype=np.uint8)
    gray[:, :80] = 40
    gray[:, 80:] = 200
    cv2_noise = np.random.default_rng(0).integers(0, 30, size=(120, 80), dtype=np.uint8)
    gray[:, :80] = np.clip(gray[:, :80].astype(np.int16) + cv2_noise, 0, 255).astype(np.uint8)

    depth = support_signals.relative_depth_map(gray)
    failures += [f for f in [
        check("relative_depth_map shape", depth.shape == gray.shape, str(depth.shape)),
        check("relative_depth_map range", depth.min() >= 0.0 and depth.max() <= 1.0,
              f"min={depth.min()} max={depth.max()}"),
    ] if f]

    flat_roi = (10, 10, 30, 30)
    textured_roi = (5, 5, 70, 110)
    c_flat = support_signals.depth_roi_confidence(flat_roi, depth)
    c_tex = support_signals.depth_roi_confidence(textured_roi, depth)
    failures += [f for f in [
        check("depth_roi_confidence prefers textured vs flat",
              c_tex > c_flat, f"textured={c_tex:.3f} flat={c_flat:.3f}"),
    ] if f]

    flags = support_signals.available()
    failures += [f for f in [
        check("available() depth_classical true", flags.get("depth_classical") is True, str(flags)),
        check("available() onnx flags false without paths",
              flags.get("depth_onnx") is False and flags.get("pose_onnx") is False, str(flags)),
    ] if f]

    failures += [f for f in [
        check("propose_pose_boxes without model", support_signals.propose_pose_boxes(
            np.zeros((64, 64, 3), dtype=np.uint8)) == [], ""),
    ] if f]

    print(("FAILED: " + ", ".join(failures)) if failures else "All checks passed.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
