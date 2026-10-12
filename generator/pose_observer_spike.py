#!/usr/bin/env python3
"""pose_observer_spike.py — Stage A offline bake-off CLI (outside Everyday Generate).

Examples (weights must already be on disk — no auto-download):

  # Status only
  python3 generator/pose_observer_spike.py --status \\
    --mediapipe-model ~/models/pose_landmarker_lite.task

  # Single frame → observation + seed proposals JSON
  python3 generator/pose_observer_spike.py \\
    --video clip.mp4 --frame 0 --backend mediapipe \\
    --mediapipe-model ~/models/pose_landmarker_lite.task \\
    --out /tmp/pose_frame0.json

  # Sample every N frames → JSONL metrics (availability, wall time, jitter)
  python3 generator/pose_observer_spike.py \\
    --video clip.mp4 --every 15 --max-frames 200 --backend mediapipe \\
    --mediapipe-model ~/models/pose_landmarker_lite.task \\
    --out /tmp/pose_metrics.jsonl

  # Roll up an existing JSONL bake-off file (no model / video needed)
  python3 generator/pose_observer_spike.py \\
    --summary /tmp/pose_metrics.jsonl

MediaPipe weights (owner downloads once, Apache-2.0 / Google terms apply to
weights separately — record license in bake-off notes):
  https://developers.google.com/mediapipe/solutions/vision/pose_landmarker

RTMPose ONNX: export yourself from MMPose; pass --onnx-model PATH.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

import cv2

# Allow `python generator/pose_observer_spike.py` from repo root.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import pose_observer as po  # noqa: E402


def _read_frame(video_path: str, frame_index: int):
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise SystemExit(f"could not open video: {video_path}")
    try:
        cap.set(cv2.CAP_PROP_POS_FRAMES, max(0, frame_index))
        ok, frame = cap.read()
        if not ok or frame is None:
            raise SystemExit(f"could not read frame {frame_index}")
        return frame
    finally:
        cap.release()


def _iter_frames(video_path: str, every: int, max_frames: int):
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise SystemExit(f"could not open video: {video_path}")
    try:
        idx = 0
        emitted = 0
        while True:
            ok, frame = cap.read()
            if not ok or frame is None:
                break
            if idx % every == 0:
                yield idx, frame
                emitted += 1
                if max_frames > 0 and emitted >= max_frames:
                    break
            idx += 1
    finally:
        cap.release()


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="PoseObserver Stage A spike (offline)")
    ap.add_argument("--status", action="store_true", help="Print backend availability and exit")
    ap.add_argument("--summary", default="", metavar="JSONL",
                    help="Roll up bake-off JSONL metrics and exit (no video/model)")
    ap.add_argument("--video", default=None, help="Input video path")
    ap.add_argument("--frame", type=int, default=0, help="Single-frame index (default 0)")
    ap.add_argument("--every", type=int, default=0, help="If >0, sample every N frames → JSONL")
    ap.add_argument("--max-frames", type=int, default=0, help="Cap sampled frames when --every set")
    ap.add_argument("--backend", default="mediapipe", choices=("mediapipe", "onnx", "rtmpose"))
    ap.add_argument("--mediapipe-model", default=os.environ.get("SAMNPLAYER_MEDIAPIPE_POSE", ""),
                    help="Path to Pose Landmarker .task (or env SAMNPLAYER_MEDIAPIPE_POSE)")
    ap.add_argument("--onnx-model", default=os.environ.get("SAMNPLAYER_RTMPOSE_ONNX", ""),
                    help="Path to RTMPose/similar ONNX (or env SAMNPLAYER_RTMPOSE_ONNX)")
    ap.add_argument("--out", default="", help="Write JSON / JSONL here (stdout if empty)")
    ap.add_argument("--seeds", action="store_true", help="Also attach seed proposals on single-frame out")
    args = ap.parse_args(argv)

    mp_path = args.mediapipe_model or None
    onnx_path = args.onnx_model or None

    if args.status:
        st = po.backend_status(mediapipe_model=mp_path, onnx_model=onnx_path)
        print(json.dumps(st, indent=2))
        return 0

    if args.summary:
        rows = []
        with open(args.summary, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                rows.append(json.loads(line))
        summary = po.summarize_metrics_rows(rows)
        text = json.dumps(summary, indent=2, sort_keys=True) + "\n"
        if args.out:
            with open(args.out, "w", encoding="utf-8") as out_f:
                out_f.write(text)
        else:
            sys.stdout.write(text)
        return 0

    if not args.video:
        ap.error("--video required unless --status or --summary")

    backend = "onnx" if args.backend in ("onnx", "rtmpose") else "mediapipe"

    if args.every and args.every > 0:
        rows = []
        prev_lms = None
        for idx, frame in _iter_frames(args.video, args.every, args.max_frames):
            # crude timestamp from frame index @ unknown fps — spike uses index*ms later if needed
            obs = po.observe(
                frame, backend=backend, timestamp_ms=idx,
                mediapipe_model=mp_path, onnx_model=onnx_path,
            )
            jitter = 0.0
            if prev_lms and obs.people:
                jitter = po.jitter_px(
                    prev_lms, obs.people[0].landmarks, obs.frame_width, obs.frame_height,
                )
                prev_lms = obs.people[0].landmarks
            elif obs.people:
                prev_lms = obs.people[0].landmarks
            seeds = po.observation_to_seed_proposals(obs)
            rows.append({
                "frame": idx,
                "people": len(obs.people),
                "landmark_count": sum(len(p.landmarks) for p in obs.people),
                "wall_time_ms": round(obs.wall_time_ms, 2),
                "jitter_px": round(jitter, 3),
                "seed_count": len(seeds),
                "top_seed": (
                    {"class": seeds[0].class_id, "confidence": round(seeds[0].confidence, 3)}
                    if seeds else None
                ),
                "error": obs.error,
                "backend": obs.backend,
                "model": obs.model.family,
            })
        text = "\n".join(json.dumps(r, sort_keys=True) for r in rows) + ("\n" if rows else "")
        if args.out:
            with open(args.out, "w", encoding="utf-8") as f:
                f.write(text)
        else:
            sys.stdout.write(text)
        return 0

    frame = _read_frame(args.video, args.frame)
    obs = po.observe(
        frame, backend=backend, timestamp_ms=args.frame,
        mediapipe_model=mp_path, onnx_model=onnx_path,
    )
    payload = obs.to_dict()
    if args.seeds:
        payload["seed_proposals"] = [s.__dict__ for s in po.observation_to_seed_proposals(obs)]
    text = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    return 0 if not obs.error else 2


if __name__ == "__main__":
    raise SystemExit(main())
