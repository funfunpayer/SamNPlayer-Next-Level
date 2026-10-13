#!/usr/bin/env python3
"""pose_observer.py — Stage A PoseObserver research spike (offline only).

Implements the neutral PoseObservation contract from docs/POSE_OBSERVER.md.
Optional backends:

* ``mediapipe`` — MediaPipe Tasks Pose Landmarker (if installed + model path)
* ``onnx`` — user-supplied RTMPose/similar ONNX (if onnxruntime + weights path)

Rules (hard):
- Outside Everyday Generate — never imported by the default Generate path.
- No automatic model download, no telemetry, no bundled weights.
- Missing runtime/weights → empty observation / clear error, not a crash into CSRT.
- Output is proposals only; never writes a Funscript.

Owner bake-off: run ``pose_observer_spike.py`` on frozen clips and compare
against classical motion candidates (MT-Seed baseline).
"""

from __future__ import annotations

import json
import math
import os
import time
from dataclasses import asdict, dataclass, field
from typing import Any, Dict, List, Optional, Sequence, Tuple

# MediaPipe Pose Landmarker (33) — subset we care about for body-part seeds.
# Tip/glans is NOT in the model; we only emit a weak hip-midline torso proposal.
MEDIAPIPE_LANDMARK_NAMES = (
    "nose", "left_eye_inner", "left_eye", "left_eye_outer", "right_eye_inner",
    "right_eye", "right_eye_outer", "left_ear", "right_ear", "mouth_left",
    "mouth_right", "left_shoulder", "right_shoulder", "left_elbow", "right_elbow",
    "left_wrist", "right_wrist", "left_pinky", "right_pinky", "left_index",
    "right_index", "left_thumb", "right_thumb", "left_hip", "right_hip",
    "left_knee", "right_knee", "left_ankle", "right_ankle", "left_heel",
    "right_heel", "left_foot_index", "right_foot_index",
)

# Product body-part ids (docs/BODY_REGIONS.md) ← pose landmark anchors.
BODY_PART_FROM_POSE = {
    "mouth": ("mouth_left", "mouth_right", "nose"),
    "face": ("nose", "left_ear", "right_ear"),
    "hand_1": ("left_wrist", "left_index", "left_pinky"),
    "hand_2": ("right_wrist", "right_index", "right_pinky"),
    "breasts": ("left_shoulder", "right_shoulder"),  # weak upper-torso proxy
}


@dataclass
class Landmark:
    name: str
    x_norm: float
    y_norm: float
    confidence: float


@dataclass
class PersonPose:
    landmarks: List[Landmark] = field(default_factory=list)
    bbox: Optional[Tuple[float, float, float, float]] = None  # x_norm,y_norm,w_norm,h_norm
    confidence: float = 0.0
    person_id: Optional[int] = None


@dataclass
class ModelInfo:
    family: str
    version: str = ""
    weights_id: str = ""


@dataclass
class PoseObservation:
    timestamp_ms: int
    frame_width: int
    frame_height: int
    people: List[PersonPose] = field(default_factory=list)
    model: ModelInfo = field(default_factory=lambda: ModelInfo(family="none"))
    wall_time_ms: float = 0.0
    backend: str = "none"
    error: str = ""

    def to_dict(self) -> Dict[str, Any]:
        d = asdict(self)
        # bbox tuples → lists for JSON
        for p in d.get("people") or []:
            if p.get("bbox") is not None:
                p["bbox"] = list(p["bbox"])
        return d


@dataclass
class SeedProposal:
    """Pixel ROI seed for MT-Seed / Tip+body-part ranking (proposal only)."""
    class_id: str
    x: int
    y: int
    w: int
    h: int
    confidence: float
    provenance: str  # e.g. mediapipe:hand_1


def clamp01(v: float) -> float:
    return max(0.0, min(1.0, float(v)))


def norm_to_pixel_box(
    x_norm: float, y_norm: float, w_norm: float, h_norm: float,
    frame_w: int, frame_h: int,
) -> Tuple[int, int, int, int]:
    """Normalized box (x,y,w,h in 0..1) → integer pixel ROI, clipped to frame."""
    x = int(round(clamp01(x_norm) * frame_w))
    y = int(round(clamp01(y_norm) * frame_h))
    w = max(1, int(round(max(0.0, w_norm) * frame_w)))
    h = max(1, int(round(max(0.0, h_norm) * frame_h)))
    if x + w > frame_w:
        w = max(1, frame_w - x)
    if y + h > frame_h:
        h = max(1, frame_h - y)
    return x, y, w, h


def landmarks_to_bbox_norm(landmarks: Sequence[Landmark], pad: float = 0.04) -> Optional[Tuple[float, float, float, float]]:
    usable = [lm for lm in landmarks if lm.confidence >= 0.2]
    if not usable:
        return None
    xs = [lm.x_norm for lm in usable]
    ys = [lm.y_norm for lm in usable]
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    x0 = clamp01(x0 - pad)
    y0 = clamp01(y0 - pad)
    x1 = clamp01(x1 + pad)
    y1 = clamp01(y1 + pad)
    return (x0, y0, max(1e-3, x1 - x0), max(1e-3, y1 - y0))


def _lm_map(person: PersonPose) -> Dict[str, Landmark]:
    return {lm.name: lm for lm in person.landmarks}


def _anchor_box(
    frame_w: int, frame_h: int,
    points: Sequence[Landmark],
    class_id: str,
    provenance: str,
    box_frac: float = 0.08,
) -> Optional[SeedProposal]:
    pts = [p for p in points if p.confidence >= 0.25]
    if not pts:
        return None
    cx = sum(p.x_norm for p in pts) / len(pts)
    cy = sum(p.y_norm for p in pts) / len(pts)
    conf = sum(p.confidence for p in pts) / len(pts)
    side = box_frac
    x_norm = clamp01(cx - side / 2)
    y_norm = clamp01(cy - side / 2)
    x, y, w, h = norm_to_pixel_box(x_norm, y_norm, side, side, frame_w, frame_h)
    return SeedProposal(
        class_id=class_id, x=x, y=y, w=w, h=h,
        confidence=float(conf), provenance=provenance,
    )


def observation_to_seed_proposals(obs: PoseObservation) -> List[SeedProposal]:
    """Derive ranked body-part ROI seeds from an observation (Tip = weak hip midline)."""
    if obs.frame_width <= 0 or obs.frame_height <= 0 or not obs.people:
        return []
    person = max(obs.people, key=lambda p: p.confidence)
    lm = _lm_map(person)
    fam = obs.model.family or obs.backend or "pose"
    out: List[SeedProposal] = []

    for class_id, names in BODY_PART_FROM_POSE.items():
        pts = [lm[n] for n in names if n in lm]
        seed = _anchor_box(
            obs.frame_width, obs.frame_height, pts, class_id,
            provenance=f"{fam}:{class_id}",
            box_frac=0.10 if class_id in ("face", "breasts") else 0.07,
        )
        if seed:
            # Shoulders→breasts is a weak proxy — damp confidence.
            if class_id == "breasts":
                seed.confidence *= 0.55
            out.append(seed)

    # Weak Tip / lower-torso proposal from hip midline (not a real glans landmark).
    hips = [lm[n] for n in ("left_hip", "right_hip") if n in lm]
    if len(hips) == 2:
        cx = (hips[0].x_norm + hips[1].x_norm) / 2
        cy = (hips[0].y_norm + hips[1].y_norm) / 2 + 0.06
        conf = min(hips[0].confidence, hips[1].confidence) * 0.35
        side = 0.09
        x, y, w, h = norm_to_pixel_box(
            clamp01(cx - side / 2), clamp01(cy - side / 2), side, side,
            obs.frame_width, obs.frame_height,
        )
        out.append(SeedProposal(
            class_id="penis", x=x, y=y, w=w, h=h,
            confidence=float(conf), provenance=f"{fam}:hip_midline_weak_tip",
        ))

    out.sort(key=lambda s: s.confidence, reverse=True)
    return out


def parse_observation(data: Dict[str, Any]) -> PoseObservation:
    """Parse / validate a PoseObservation dict (fixture-friendly)."""
    if not isinstance(data, dict):
        raise ValueError("observation must be an object")
    people_in = data.get("people") or []
    people: List[PersonPose] = []
    for p in people_in:
        lms = [
            Landmark(
                name=str(lm.get("name", "")),
                x_norm=clamp01(float(lm.get("x_norm", 0))),
                y_norm=clamp01(float(lm.get("y_norm", 0))),
                confidence=clamp01(float(lm.get("confidence", 0))),
            )
            for lm in (p.get("landmarks") or [])
        ]
        bbox = p.get("bbox")
        bbox_t = None
        if bbox is not None and len(bbox) >= 4:
            bbox_t = (
                clamp01(float(bbox[0])), clamp01(float(bbox[1])),
                max(0.0, float(bbox[2])), max(0.0, float(bbox[3])),
            )
        people.append(PersonPose(
            landmarks=lms,
            bbox=bbox_t,
            confidence=clamp01(float(p.get("confidence", 0))),
            person_id=p.get("person_id"),
        ))
    model_in = data.get("model") or {}
    return PoseObservation(
        timestamp_ms=int(data.get("timestamp_ms", 0)),
        frame_width=int(data.get("frame_width", 0)),
        frame_height=int(data.get("frame_height", 0)),
        people=people,
        model=ModelInfo(
            family=str(model_in.get("family", "unknown")),
            version=str(model_in.get("version", "")),
            weights_id=str(model_in.get("weights_id", "")),
        ),
        wall_time_ms=float(data.get("wall_time_ms", 0)),
        backend=str(data.get("backend", "")),
        error=str(data.get("error", "")),
    )


def backend_status(mediapipe_model: Optional[str] = None, onnx_model: Optional[str] = None) -> Dict[str, Any]:
    """What Stage A can run on this machine (no downloads)."""
    st: Dict[str, Any] = {
        "mediapipe_import": False,
        "mediapipe_model": False,
        "onnxruntime": False,
        "onnx_model": False,
    }
    try:
        import mediapipe  # noqa: F401
        st["mediapipe_import"] = True
    except ImportError:
        pass
    if mediapipe_model and os.path.isfile(mediapipe_model):
        st["mediapipe_model"] = True
    try:
        import onnxruntime  # noqa: F401
        st["onnxruntime"] = True
    except ImportError:
        pass
    if onnx_model and os.path.isfile(onnx_model):
        st["onnx_model"] = True
    return st


def observe_mediapipe(
    frame_bgr,
    timestamp_ms: int = 0,
    model_path: Optional[str] = None,
) -> PoseObservation:
    """Run MediaPipe Pose Landmarker if available; else empty + error string."""
    h, w = frame_bgr.shape[:2]
    t0 = time.perf_counter()
    if not model_path or not os.path.isfile(model_path):
        return PoseObservation(
            timestamp_ms=timestamp_ms, frame_width=w, frame_height=h,
            model=ModelInfo(family="mediapipe"), backend="mediapipe",
            error="mediapipe model path missing — place Pose Landmarker .task locally (no auto-download)",
            wall_time_ms=(time.perf_counter() - t0) * 1000,
        )
    try:
        from mediapipe.tasks.python import vision
        from mediapipe.tasks.python.core import base_options as mp_base
        import mediapipe as mp
    except ImportError:
        return PoseObservation(
            timestamp_ms=timestamp_ms, frame_width=w, frame_height=h,
            model=ModelInfo(family="mediapipe"), backend="mediapipe",
            error="mediapipe not installed — pip install mediapipe (optional Stage A dep)",
            wall_time_ms=(time.perf_counter() - t0) * 1000,
        )

    try:
        options = vision.PoseLandmarkerOptions(
            base_options=mp_base.BaseOptions(model_asset_path=model_path),
            running_mode=vision.RunningMode.IMAGE,
            num_poses=2,
            min_pose_detection_confidence=0.4,
            min_pose_presence_confidence=0.4,
            min_tracking_confidence=0.4,
        )
        with vision.PoseLandmarker.create_from_options(options) as landmarker:
            rgb = frame_bgr[:, :, ::-1].copy()
            mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
            result = landmarker.detect(mp_image)
    except Exception as exc:
        return PoseObservation(
            timestamp_ms=timestamp_ms, frame_width=w, frame_height=h,
            model=ModelInfo(family="mediapipe", weights_id=os.path.basename(model_path)),
            backend="mediapipe",
            error=f"mediapipe detect failed: {exc}",
            wall_time_ms=(time.perf_counter() - t0) * 1000,
        )

    people: List[PersonPose] = []
    pose_list = getattr(result, "pose_landmarks", None) or []
    for pi, pose in enumerate(pose_list):
        lms: List[Landmark] = []
        for i, pt in enumerate(pose):
            name = MEDIAPIPE_LANDMARK_NAMES[i] if i < len(MEDIAPIPE_LANDMARK_NAMES) else f"lm_{i}"
            vis = float(getattr(pt, "visibility", getattr(pt, "presence", 0.0)) or 0.0)
            lms.append(Landmark(
                name=name,
                x_norm=clamp01(float(pt.x)),
                y_norm=clamp01(float(pt.y)),
                confidence=clamp01(vis),
            ))
        bbox = landmarks_to_bbox_norm(lms)
        conf = sum(lm.confidence for lm in lms) / max(1, len(lms))
        people.append(PersonPose(landmarks=lms, bbox=bbox, confidence=conf, person_id=pi))

    return PoseObservation(
        timestamp_ms=timestamp_ms, frame_width=w, frame_height=h,
        people=people,
        model=ModelInfo(
            family="mediapipe",
            version="tasks-pose-landmarker",
            weights_id=os.path.basename(model_path),
        ),
        backend="mediapipe",
        wall_time_ms=(time.perf_counter() - t0) * 1000,
    )


def observe_onnx_keypoints(
    frame_bgr,
    timestamp_ms: int = 0,
    model_path: Optional[str] = None,
    input_size: int = 256,
) -> PoseObservation:
    """Best-effort RTMPose-style ONNX: expects output reshapeable to (K, 3) = x,y,score.

    Many exports differ; if the layout is unknown we return error + empty people
    rather than inventing landmarks. Stage A is honest about unsupported layouts.
    """
    h, w = frame_bgr.shape[:2]
    t0 = time.perf_counter()
    if not model_path or not os.path.isfile(model_path):
        return PoseObservation(
            timestamp_ms=timestamp_ms, frame_width=w, frame_height=h,
            model=ModelInfo(family="rtmpose_onnx"), backend="onnx",
            error="onnx model path missing — export RTMPose locally (no auto-download)",
            wall_time_ms=(time.perf_counter() - t0) * 1000,
        )
    try:
        import onnxruntime as ort
        import cv2
        import numpy as np
    except ImportError:
        return PoseObservation(
            timestamp_ms=timestamp_ms, frame_width=w, frame_height=h,
            model=ModelInfo(family="rtmpose_onnx"), backend="onnx",
            error="onnxruntime not installed",
            wall_time_ms=(time.perf_counter() - t0) * 1000,
        )

    try:
        session = ort.InferenceSession(model_path, providers=["CPUExecutionProvider"])
        inp = session.get_inputs()[0]
        size = input_size
        if len(inp.shape) == 4 and inp.shape[2] and int(inp.shape[2]) > 0:
            size = int(inp.shape[2])
        resized = cv2.resize(frame_bgr, (size, size))
        rgb = cv2.cvtColor(resized, cv2.COLOR_BGR2RGB).astype("float32") / 255.0
        tensor = rgb.transpose(2, 0, 1)[None, ...]
        raw = session.run(None, {inp.name: tensor})[0]
        arr = __import__("numpy").asarray(raw, dtype=float)
    except Exception as exc:
        return PoseObservation(
            timestamp_ms=timestamp_ms, frame_width=w, frame_height=h,
            model=ModelInfo(family="rtmpose_onnx", weights_id=os.path.basename(model_path)),
            backend="onnx", error=f"onnx inference failed: {exc}",
            wall_time_ms=(time.perf_counter() - t0) * 1000,
        )

    # Accept (1, K, 3) or (K, 3) with normalized or pixel coords.
    if arr.ndim == 3:
        arr = arr[0]
    if arr.ndim != 2 or arr.shape[1] < 3:
        return PoseObservation(
            timestamp_ms=timestamp_ms, frame_width=w, frame_height=h,
            model=ModelInfo(family="rtmpose_onnx", weights_id=os.path.basename(model_path)),
            backend="onnx",
            error=f"unsupported ONNX layout shape={getattr(arr, 'shape', None)} — need (K,3) x,y,score",
            wall_time_ms=(time.perf_counter() - t0) * 1000,
        )

    lms: List[Landmark] = []
    max_xy = float(arr[:, :2].max()) if arr.size else 0.0
    for i, row in enumerate(arr):
        x, y, score = float(row[0]), float(row[1]), float(row[2])
        if max_xy > 1.5:
            x, y = x / float(size), y / float(size)
        name = MEDIAPIPE_LANDMARK_NAMES[i] if i < len(MEDIAPIPE_LANDMARK_NAMES) else f"kpt_{i}"
        lms.append(Landmark(name=name, x_norm=clamp01(x), y_norm=clamp01(y), confidence=clamp01(score)))

    bbox = landmarks_to_bbox_norm(lms)
    conf = sum(lm.confidence for lm in lms) / max(1, len(lms))
    return PoseObservation(
        timestamp_ms=timestamp_ms, frame_width=w, frame_height=h,
        people=[PersonPose(landmarks=lms, bbox=bbox, confidence=conf, person_id=0)],
        model=ModelInfo(family="rtmpose_onnx", weights_id=os.path.basename(model_path)),
        backend="onnx",
        wall_time_ms=(time.perf_counter() - t0) * 1000,
    )


def observe(
    frame_bgr,
    backend: str = "mediapipe",
    timestamp_ms: int = 0,
    mediapipe_model: Optional[str] = None,
    onnx_model: Optional[str] = None,
) -> PoseObservation:
    backend = (backend or "mediapipe").lower().strip()
    if backend in ("mediapipe", "mp"):
        return observe_mediapipe(frame_bgr, timestamp_ms=timestamp_ms, model_path=mediapipe_model)
    if backend in ("onnx", "rtmpose", "rtmpose_onnx"):
        return observe_onnx_keypoints(frame_bgr, timestamp_ms=timestamp_ms, model_path=onnx_model)
    h, w = frame_bgr.shape[:2]
    return PoseObservation(
        timestamp_ms=timestamp_ms, frame_width=w, frame_height=h,
        model=ModelInfo(family="none"), backend=backend,
        error=f"unknown backend {backend!r} — use mediapipe|onnx",
    )


def jitter_px(prev: Sequence[Landmark], cur: Sequence[Landmark], frame_w: int, frame_h: int) -> float:
    """Mean pixel displacement for landmarks present in both frames (Stage A metric)."""
    a = {lm.name: lm for lm in prev}
    dists = []
    for lm in cur:
        if lm.name not in a or lm.confidence < 0.3 or a[lm.name].confidence < 0.3:
            continue
        dx = (lm.x_norm - a[lm.name].x_norm) * frame_w
        dy = (lm.y_norm - a[lm.name].y_norm) * frame_h
        dists.append(math.hypot(dx, dy))
    if not dists:
        return 0.0
    return float(sum(dists) / len(dists))


def filter_seeds_by_confidence(
    seeds: Sequence[SeedProposal],
    min_confidence: float = 0.25,
) -> List[SeedProposal]:
    """Drop weak proposals before MT-Seed ranking (Stage A helper)."""
    return [s for s in seeds if s.confidence >= min_confidence]


def fuse_proposal_lists(
    *lists: Sequence[SeedProposal],
    min_confidence: float = 0.0,
) -> List[SeedProposal]:
    """Merge classical + pose seeds; keep provenance; rank by confidence desc.

    Deterministic: equal confidence → provenance then class_id. Does not
    silently commit — caller / GUI still chooses.
    """
    merged: List[SeedProposal] = []
    for lst in lists:
        for s in lst:
            if s.confidence >= min_confidence:
                merged.append(s)
    merged.sort(key=lambda s: (-s.confidence, s.provenance, s.class_id))
    return merged


def summarize_metrics_rows(rows: Sequence[Dict[str, Any]]) -> Dict[str, Any]:
    """Roll up spike JSONL rows into bake-off observer metrics (no video needed)."""
    n = len(rows)
    if n == 0:
        return {
            "frames": 0,
            "availability": 0.0,
            "mean_wall_time_ms": 0.0,
            "mean_jitter_px": 0.0,
            "mean_seed_count": 0.0,
            "error_frames": 0,
            "backends": [],
        }
    with_people = sum(1 for r in rows if int(r.get("people") or 0) > 0)
    wall = [float(r.get("wall_time_ms") or 0) for r in rows]
    jit = [float(r.get("jitter_px") or 0) for r in rows]
    seeds = [float(r.get("seed_count") or 0) for r in rows]
    errs = sum(1 for r in rows if r.get("error"))
    backends = sorted({str(r.get("backend") or "") for r in rows if r.get("backend")})
    return {
        "frames": n,
        "availability": with_people / n,
        "mean_wall_time_ms": sum(wall) / n,
        "p95_wall_time_ms": sorted(wall)[min(n - 1, int(0.95 * (n - 1)))] if n else 0.0,
        "mean_jitter_px": sum(jit) / n,
        "mean_seed_count": sum(seeds) / n,
        "error_frames": errs,
        "backends": backends,
    }


def observation_json(obs: PoseObservation) -> str:
    return json.dumps(obs.to_dict(), indent=2, sort_keys=True)
