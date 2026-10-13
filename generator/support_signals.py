#!/usr/bin/env python3
"""support_signals.py — optional classical + ONNX helpers for depth/pose hints.

These signals **propose** ranking hints only; they never write a `.funscript`
and are not on the default pipeline path (see docs/AI_ADAPTER.md,
docs/DEPTH_POSE.md).

Classical relative depth is a **proxy** from image cues (gradient + local
variance), not metric monocular depth. Optional ONNX paths are stubs until a
model contract is validated on golden clips.

No bundled models, no auto-download, no telemetry. Missing onnxruntime or
model files → silent fallback (empty lists / classical-only).
"""

import argparse
import json
import os
import sys

import cv2
import numpy as np

# Expected future pose ONNX output (stub): tensor reshapable to (N, 6) with
# [x0, y0, x1, y1, confidence, class_id] in normalized 0..1 image coords,
# same spirit as ai_roi.decode_detections — or (N, 5) [x0,y0,x1,y1,conf].


def available(depth_onnx_path=None, pose_onnx_path=None):
    """Report which supporting-signal backends are usable on this machine."""
    flags = {
        "depth_classical": True,
        "depth_onnx": False,
        "pose_onnx": False,
    }
    if not depth_onnx_path and not pose_onnx_path:
        pass
    try:
        import onnxruntime  # noqa: F401
    except ImportError:
        return flags
    if depth_onnx_path and os.path.isfile(depth_onnx_path):
        flags["depth_onnx"] = True
    if pose_onnx_path and os.path.isfile(pose_onnx_path):
        flags["pose_onnx"] = True
    return flags


def load_onnx_session(path):
    """Return an onnxruntime.InferenceSession or None if unavailable."""
    if not path or not os.path.isfile(path):
        return None
    try:
        import onnxruntime
    except ImportError:
        return None
    try:
        return onnxruntime.InferenceSession(path, providers=["CPUExecutionProvider"])
    except Exception:
        return None


def _normalize01(arr):
    lo = float(np.min(arr))
    hi = float(np.max(arr))
    if hi <= lo:
        return np.zeros_like(arr, dtype=np.float32)
    return ((arr - lo) / (hi - lo)).astype(np.float32)


def relative_depth_map(gray_u8):
    """Classical monocular depth **proxy** in [0, 1], shape HxW float32.

    Combines normalized gradient magnitude with normalized inverse local
    variance (texture / edge cues). This is not metric depth — only a soft
    ranking signal for ROI candidates.
    """
    if gray_u8.ndim != 2:
        raise ValueError("relative_depth_map expects a single-channel uint8 image")
    gray = gray_u8.astype(np.float32) / 255.0
    gx = cv2.Sobel(gray, cv2.CV_32F, 1, 0, ksize=3)
    gy = cv2.Sobel(gray, cv2.CV_32F, 0, 1, ksize=3)
    grad = np.sqrt(gx * gx + gy * gy)
    blur = cv2.blur(gray, (15, 15))
    local_var = cv2.blur((gray - blur) ** 2, (15, 15))
    inv_var = 1.0 / (local_var + 1e-4)
    proxy = 0.5 * _normalize01(grad) + 0.5 * _normalize01(inv_var)
    return np.clip(proxy, 0.0, 1.0).astype(np.float32)


def depth_roi_confidence(roi, depth_map):
    """Score 0..1: higher when ROI depth contrast vs a padded surround differs."""
    if depth_map is None or depth_map.size == 0:
        return 0.0
    x, y, w, h = (int(roi[0]), int(roi[1]), int(roi[2]), int(roi[3]))
    H, W = depth_map.shape[:2]
    if w <= 0 or h <= 0:
        return 0.0
    x0, y0 = max(0, x), max(0, y)
    x1, y1 = min(W, x + w), min(H, y + h)
    if x1 <= x0 or y1 <= y0:
        return 0.0
    inner = depth_map[y0:y1, x0:x1]
    inner_mean = float(inner.mean())
    pad = max(4, min(w, h) // 4)
    ox0 = max(0, x0 - pad)
    oy0 = max(0, y0 - pad)
    ox1 = min(W, x1 + pad)
    oy1 = min(H, y1 + pad)
    outer = depth_map[oy0:oy1, ox0:ox1].copy()
    outer[y0 - oy0 : y1 - oy0, x0 - ox0 : x1 - ox0] = np.nan
    if not np.any(np.isfinite(outer)):
        surround_mean = float(depth_map.mean())
    else:
        surround_mean = float(np.nanmean(outer))
    contrast = abs(inner_mean - surround_mean)
    texture = float(inner.std())
    score = 0.7 * min(1.0, contrast * 4.0) + 0.3 * min(1.0, texture * 3.0)
    return float(np.clip(score, 0.0, 1.0))


def propose_pose_boxes(frame_bgr, model_path=None):
    """Optional pose/person box proposals — stub until a model is validated.

    Without model_path (or missing onnxruntime/file), returns [].
    With a session, runs inference but only returns boxes when output matches
    the documented (N, 6) or (N, 5) normalized box layout; otherwise [].
    """
    session = load_onnx_session(model_path) if model_path else None
    if session is None:
        return []
    h, w = frame_bgr.shape[:2]
    inp = session.get_inputs()[0]
    name = inp.name
    shape = inp.shape
    size = 640
    if len(shape) == 4 and shape[2] and int(shape[2]) > 0:
        size = int(shape[2])
    resized = cv2.resize(frame_bgr, (size, size))
    rgb = cv2.cvtColor(resized, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
    tensor = np.expand_dims(np.transpose(rgb, (2, 0, 1)), axis=0)
    try:
        raw = session.run(None, {name: tensor})[0]
    except Exception:
        return []
    arr = np.asarray(raw, dtype=float)
    if arr.size == 0:
        return []
    if arr.ndim == 1:
        arr = arr.reshape(-1, 6)
    elif arr.ndim > 2:
        arr = arr.reshape(-1, arr.shape[-1])
    ncol = arr.shape[1]
    boxes = []
    for row in arr:
        if ncol >= 6:
            x0, y0, x1, y1, conf = row[0], row[1], row[2], row[3], row[4]
        elif ncol >= 5:
            x0, y0, x1, y1, conf = row[0], row[1], row[2], row[3], row[4]
        else:
            continue
        if conf < 0.35 or x1 <= x0 or y1 <= y0:
            continue
        if max(x0, y0, x1, y1) > 1.5:
            continue  # unknown pixel-space export — stay honest
        px0 = int(x0 * w)
        py0 = int(y0 * h)
        px1 = int(x1 * w)
        py1 = int(y1 * h)
        boxes.append((px0, py0, max(1, px1 - px0), max(1, py1 - py0)))
    return boxes


def _read_video_frame(video_path, frame_index):
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"Could not open video: {video_path}")
    try:
        cap.set(cv2.CAP_PROP_POS_FRAMES, max(0, frame_index))
        ok, frame = cap.read()
        if not ok or frame is None:
            raise RuntimeError(f"Could not read frame {frame_index} from {video_path}")
        return frame
    finally:
        cap.release()


def cli_summary(video_path, frame_index, depth_onnx_path=None, pose_onnx_path=None):
    frame = _read_video_frame(video_path, frame_index)
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    depth = relative_depth_map(gray)
    h, w = gray.shape
    sample_roi = (w // 4, h // 4, w // 2, h // 2)
    summary = {
        "available": available(depth_onnx_path=depth_onnx_path, pose_onnx_path=pose_onnx_path),
        "frame": int(frame_index),
        "depth_map_shape": [int(depth.shape[0]), int(depth.shape[1])],
        "depth_map_min": float(depth.min()),
        "depth_map_max": float(depth.max()),
        "sample_roi_confidence": depth_roi_confidence(sample_roi, depth),
        "pose_box_count": len(propose_pose_boxes(frame, model_path=pose_onnx_path)),
    }
    return summary


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="Print JSON availability flags only (no video).")
    ap.add_argument("--video", help="Video path for frame sample.")
    ap.add_argument("--frame", type=int, default=0, help="Frame index (default 0).")
    ap.add_argument("--depth-onnx", default=None, help="Optional depth ONNX model path.")
    ap.add_argument("--pose-onnx", default=None, help="Optional pose ONNX model path.")
    args = ap.parse_args()

    if args.check:
        print(json.dumps(available(depth_onnx_path=args.depth_onnx,
                                   pose_onnx_path=args.pose_onnx)))
        return

    if not args.video:
        ap.error("--video is required unless --check is set")

    try:
        summary = cli_summary(args.video, args.frame,
                              depth_onnx_path=args.depth_onnx,
                              pose_onnx_path=args.pose_onnx)
    except RuntimeError as exc:
        print(f"support_signals failed: {exc}", file=sys.stderr)
        sys.exit(1)
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
