#!/usr/bin/env python3
"""contact_points.py - asks one or more LOCAL teachers where the stroke
contact is and writes `<clip>.contact.json` for the rhythm grid
(`generate --rhythm-grid --contact-points <file>`, Options.ContactPointsFile).

VLM1 of docs/VLM_TEACHER_PLAN.md. The engine uses a point only where the
CSRT box is further from it than the grid's search radius (3 cells), so a
wrong point can not pull a working track away; measured on the multi-person
clip: windowed r 0.304 -> 0.406 with NudeNet alone (real Go TrackROI);
goldens unchanged (clip_ausschnitt bit-identical, clip_voll same r).

Teachers (use any combination - the Owner's "ask several per video"):
- NudeNet (`--nudenet`): body-part detector, runs every --step-s seconds.
  Contact rule: exposed penis centre, else the cleavage point between a
  breast pair, else nothing. (A face/mouth rule measured 0 % correct in
  titjob scenes and is deliberately absent.) Optional dependency: the
  package is MIT and bundles its ONNX model; install it next to the
  generator's OpenCV with `pip install nudenet --no-deps onnxruntime`.
- VLM probe results (`--vlm clip.vlm.json`, repeatable): the contact box
  centre of each keyframe, e.g. Qwen2.5-VL and Qwen3-VL runs of vlm_probe.py.
- Or let this script ask the VLMs directly (`--teacher backend:model`,
  repeatable): ollama, lmstudio, colibri (very large MoE models incl.
  vision, streamed from disk) or vllm; results are cached per video.
- Our own detector (`--onnx contact_detector.onnx`, repeatable): the RF-DETR
  model contact_detector.py trains from confirmed marks; runs every
  --step-s seconds like NudeNet (needs onnxruntime).

Every point records how many distinct teachers put a point within
--agree-cells grid cells and --agree-ms of it (`agree`). `--contact-min-agree
2` on the Generate side uses only consensus points; those are also the
training-data candidates (reviewed:false) for our own detector later.

Local only: frames never leave the machine, no model in the repo, nothing
downloads by itself.
"""

import argparse
import json
import math
import os
import sys

import cv2

GRID_COLS, GRID_ROWS = 16, 9
MIN_SCORE = 0.3


def nudenet_detector():
    """NudeNet's detect(frame) or a clear error if the package is missing."""
    try:
        from nudenet import NudeDetector
    except ImportError as e:  # pragma: no cover - depends on the machine
        raise RuntimeError(
            "NudeNet is not installed - `pip install nudenet --no-deps onnxruntime` "
            "(see docs/VLM_MODELS.md)") from e
    return NudeDetector().detect


def contact_from_parts(dets, w, h, min_score=MIN_SCORE):
    """NudeNet detections -> (x, y normalized, rule, score, box) or None.

    dets: [{"class": str, "score": float, "box": [x, y, w, h] px}]."""
    d = [x for x in dets if x.get("score", 0) >= min_score]
    g = [x for x in d if x["class"] == "MALE_GENITALIA_EXPOSED"]
    if g:
        best = max(g, key=lambda x: x["score"])
        bx, by, bw, bh = best["box"]
        return ((bx + bw / 2) / w, (by + bh / 2) / h, "penis", best["score"],
                [bx / w, by / h, (bx + bw) / w, (by + bh) / h])
    br = [x for x in d if x["class"] == "FEMALE_BREAST_EXPOSED"]
    pair = None
    for i in range(len(br)):
        for j in range(i + 1, len(br)):
            a, b = br[i]["box"], br[j]["box"]
            dx = abs((a[0] + a[2] / 2) - (b[0] + b[2] / 2))
            # One person's pair: side by side, not further apart than
            # ~2.5 breast widths (two people's breasts are further apart).
            if dx < 2.5 * max(a[2], b[2]) and (pair is None or dx < pair[0]):
                pair = (dx, br[i], br[j])
    if pair:
        _, p, q = pair
        a, b = p["box"], q["box"]
        x = ((a[0] + a[2] / 2) + (b[0] + b[2] / 2)) / 2 / w
        y = max(a[1] + a[3] * 0.6, b[1] + b[3] * 0.6) / h
        return x, y, "cleavage", min(p["score"], q["score"]), None
    return None


def sampled_points(video, contact_fn, source, step_s=0.5, log=print):
    """Runs a per-frame teacher every step_s seconds. contact_fn(frame, w, h)
    returns (x, y, rule, score, box) - normalized - or None."""
    cap = cv2.VideoCapture(video)
    if not cap.isOpened():
        raise RuntimeError(f"Could not open video: {video}")
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    n = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)
    w, h = cap.get(cv2.CAP_PROP_FRAME_WIDTH), cap.get(cv2.CAP_PROP_FRAME_HEIGHT)
    step = max(1, int(round(step_s * fps)))
    out = []
    for i in range(0, n, step):
        cap.set(cv2.CAP_PROP_POS_FRAMES, i)
        ok, frame = cap.read()
        if not ok:
            break
        c = contact_fn(frame, w, h)
        if c:
            x, y, rule, score, box = c
            p = {"t_ms": int(round(i * 1000 / fps)), "x": round(x, 4), "y": round(y, 4),
                 "source": source, "rule": rule, "score": round(float(score), 3)}
            if box:
                p["box"] = [round(v, 4) for v in box]
            out.append(p)
    cap.release()
    log(f"{source}: {len(out)} points from {len(range(0, n, step))} frames")
    return out


def nudenet_points(video, detect, step_s=0.5, log=print):
    return sampled_points(video, lambda fr, w, h: contact_from_parts(detect(fr), w, h),
                          "nudenet", step_s, log)


# RF-DETR ONNX export (our own detector, contact_detector.py): input
# [1, 3, H, W] RGB, 0..1, ImageNet-normalized; outputs "dets" [1, Q, 4]
# normalized cxcywh and "labels" [1, Q, C(+1 background, last)] logits with
# per-class sigmoid - the same decode RF-DETR's own ONNX helper uses.
_MEAN = (0.485, 0.456, 0.406)
_STD = (0.229, 0.224, 0.225)


def decode_rfdetr(boxes_cwh, logits, n_classes, class_index, threshold=0.3):
    """Best box of one class: (score, [x0, y0, x1, y1] normalized) or None."""
    import numpy as np
    logits = np.asarray(logits, dtype=np.float64)
    if logits.ndim == 3:
        logits, boxes_cwh = logits[0], np.asarray(boxes_cwh)[0]
    if logits.shape[1] == n_classes + 1:
        logits = logits[:, :n_classes]  # drop the background slot
    scores = 1.0 / (1.0 + np.exp(-np.clip(logits[:, class_index], -88, 88)))
    q = int(np.argmax(scores))
    if scores[q] <= threshold:
        return None
    cx, cy, bw, bh = (float(v) for v in np.asarray(boxes_cwh)[q])
    box = [max(0.0, cx - bw / 2), max(0.0, cy - bh / 2), min(1.0, cx + bw / 2), min(1.0, cy + bh / 2)]
    if box[2] <= box[0] or box[3] <= box[1]:
        return None
    return float(scores[q]), box


class OnnxContactModel:
    """Our own RF-DETR contact detector as a teacher. The class list comes
    from the model card contact_detector.py writes next to the .onnx."""

    def __init__(self, onnx_path, class_name="contact", threshold=0.3, session=None):
        card = os.path.splitext(onnx_path)[0] + ".json"
        with open(card, encoding="utf-8") as f:
            self.classes = json.load(f)["classes"]
        if class_name not in self.classes:
            raise ValueError(f"class {class_name!r} not in model classes {self.classes}")
        self.class_index = self.classes.index(class_name)
        self.class_name = class_name
        self.threshold = threshold
        if session is None:
            try:
                import onnxruntime as ort
            except ImportError as e:  # pragma: no cover - depends on the machine
                raise RuntimeError("onnxruntime is not installed - `pip install onnxruntime`") from e
            session = ort.InferenceSession(onnx_path, providers=[
                p for p in ("CUDAExecutionProvider", "CPUExecutionProvider")
                if p in ort.get_available_providers()])
        self.session = session
        inp = session.get_inputs()[0]
        self.input_name = inp.name
        self.in_h, self.in_w = int(inp.shape[2]), int(inp.shape[3])
        outs = [o.name for o in session.get_outputs()]
        self.out_boxes = "dets" if "dets" in outs else outs[0]
        self.out_logits = "labels" if "labels" in outs else outs[1]

    def contact(self, frame, w, h):
        import numpy as np
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        # Bilinear, no antialias - RF-DETR's own preprocessing convention.
        x = cv2.resize(rgb, (self.in_w, self.in_h), interpolation=cv2.INTER_LINEAR)
        x = (x.astype(np.float32) / 255.0 - np.array(_MEAN, np.float32)) / np.array(_STD, np.float32)
        x = x.transpose(2, 0, 1)[None]
        boxes, logits = self.session.run([self.out_boxes, self.out_logits], {self.input_name: x})
        r = decode_rfdetr(boxes, logits, len(self.classes), self.class_index, self.threshold)
        if r is None:
            return None
        score, b = r
        return (b[0] + b[2]) / 2, (b[1] + b[3]) / 2, "detector:" + self.class_name, score, b


def vlm_points(probe):
    """Contact box centres of a vlm_probe.py result."""
    src = "vlm:" + str(probe.get("model", "?"))
    out = []
    for f in probe.get("frames", []):
        boxes = [b for b in f.get("boxes", []) if b.get("label") == "contact"]
        if not boxes:
            continue
        b = max(boxes, key=lambda b: (b["x1"] - b["x0"]) * (b["y1"] - b["y0"]))
        out.append({"t_ms": int(f["t_ms"]), "x": round((b["x0"] + b["x1"]) / 2, 4),
                    "y": round((b["y0"] + b["y1"]) / 2, 4), "source": src,
                    "box": [b["x0"], b["y0"], b["x1"], b["y1"]]})
    return out


def count_agreement(points, agree_cells=1.5, agree_ms=1500):
    """Sets p["agree"] = number of distinct teachers (incl. p's own) with a
    point within agree_ms and agree_cells grid cells of p."""
    for p in points:
        teachers = {p["source"]}
        for q in points:
            if q["source"] in teachers or abs(q["t_ms"] - p["t_ms"]) > agree_ms:
                continue
            if math.hypot((q["x"] - p["x"]) * GRID_COLS, (q["y"] - p["y"]) * GRID_ROWS) <= agree_cells:
                teachers.add(q["source"])
        p["agree"] = len(teachers)
    return points


def parse_teacher(spec):
    """"backend:model" -> (backend, model); the model id may contain ':'."""
    backend, sep, model = spec.partition(":")
    if not sep or not model:
        raise ValueError(f"teacher {spec!r}: want backend:model, e.g. ollama:qwen2.5vl:7b")
    return backend, model


def teacher_cache_path(video, backend, model):
    safe = "".join(c if c.isalnum() or c in "-._" else "_" for c in model)
    return f"{os.path.splitext(video)[0]}.{backend}_{safe}.vlm.json"


def run_vlm_teachers(video, specs, every_s=5.0, exemplar_json=None, follow=False,
                     refresh=False, post_fn=None, log=print):
    """Runs vlm_probe for each "backend:model" teacher (Ollama, LM Studio,
    Colibri, vLLM) and returns the result paths. Results are cached next to
    the video; a teacher that fails is logged and skipped, so one missing
    server does not cost the others' work."""
    import vlm_probe
    exemplars = vlm_probe.load_exemplars(exemplar_json, (), 1) if exemplar_json else []
    paths = []
    for spec in specs:
        backend, model = parse_teacher(spec)
        base_url, timeout = vlm_probe.backend_settings(backend)
        out = teacher_cache_path(video, backend, model)
        if os.path.exists(out) and not refresh:
            log(f"{spec}: cached {out}")
            paths.append(out)
            continue
        if post_fn is None and not vlm_probe.colibri_client.available(base_url):
            log(f"{spec}: no server at {base_url} - skipped")
            continue
        try:
            res = vlm_probe.probe_video(video, model, base_url, every_s, True, "auto", timeout,
                                        None, 0, post_fn=post_fn, log=log,
                                        exemplars=exemplars, follow=follow)
        except Exception as e:  # noqa: BLE001 - one teacher failing is not fatal
            log(f"{spec}: failed - {e}")
            continue
        res["backend"] = backend
        with open(out, "w", encoding="utf-8") as f:
            json.dump(res, f, indent=1)
        log(f"{spec}: {res['summary']}")
        paths.append(out)
    return paths


def build(video, use_nudenet=False, vlm_files=(), step_s=0.5, agree_cells=1.5,
          agree_ms=1500, detect=None, log=print, onnx_models=()):
    points, teachers = [], []
    if use_nudenet:
        points += nudenet_points(video, detect or nudenet_detector(), step_s, log)
        teachers.append("nudenet")
    for model in onnx_models:
        src = "detector:" + os.path.basename(getattr(model, "path", "") or model.class_name)
        points += sampled_points(video, model.contact, src, step_s, log)
        teachers.append(src)
    for path in vlm_files:
        with open(path, encoding="utf-8") as f:
            vp = vlm_points(json.load(f))
        log(f"{path}: {len(vp)} contact points")
        points += vp
        teachers += sorted({p["source"] for p in vp}) or [f"vlm:{os.path.basename(path)}"]
    if not teachers:
        raise ValueError("no teacher selected - use --nudenet, --vlm and/or --onnx")
    points.sort(key=lambda p: (p["t_ms"], p["source"]))
    count_agreement(points, agree_cells, agree_ms)
    agreed = sum(1 for p in points if p["agree"] >= 2)
    return {
        "version": 1, "tool": "contact_points", "video": os.path.basename(video),
        "teachers": teachers, "step_s": step_s,
        "agree_cells": agree_cells, "agree_ms": agree_ms,
        "points": points,
        "summary": {"points": len(points), "agree_2plus": agreed,
                    "by_source": {s: sum(1 for p in points if p["source"] == s)
                                  for s in sorted({p["source"] for p in points})}},
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--video", required=True)
    ap.add_argument("--nudenet", action="store_true", help="use the NudeNet teacher")
    ap.add_argument("--vlm", action="append", default=[],
                    help="vlm_probe.py result (.vlm.json), repeatable")
    ap.add_argument("--teacher", action="append", default=[],
                    help="ask a local VLM directly: backend:model, e.g. ollama:qwen2.5vl:7b, "
                         "colibri:glm-5.3-flash, lmstudio:<id> (repeatable; results cached)")
    ap.add_argument("--teacher-every-s", type=float, default=5.0)
    ap.add_argument("--teacher-exemplar", help="labels JSON: show the model the marked "
                                               "first keyframe (vlm_probe exemplar mode)")
    ap.add_argument("--teacher-follow", action="store_true")
    ap.add_argument("--refresh", action="store_true", help="re-run cached --teacher results")
    ap.add_argument("--onnx", action="append", default=[],
                    help="our own detector (contact_detector.py train), repeatable")
    ap.add_argument("--onnx-class", default="contact")
    ap.add_argument("--step-s", type=float, default=0.5)
    ap.add_argument("--agree-cells", type=float, default=1.5)
    ap.add_argument("--agree-ms", type=int, default=1500)
    ap.add_argument("--out")
    args = ap.parse_args(argv)
    try:
        models = []
        for path in args.onnx:
            m = OnnxContactModel(path, args.onnx_class)
            m.path = path
            models.append(m)
        vlm_files = list(args.vlm) + run_vlm_teachers(
            args.video, args.teacher, args.teacher_every_s, args.teacher_exemplar,
            args.teacher_follow, args.refresh)
        res = build(args.video, args.nudenet, vlm_files, args.step_s,
                    args.agree_cells, args.agree_ms, onnx_models=models)
    except (RuntimeError, ValueError) as e:
        print(str(e), file=sys.stderr)
        return 2
    out = args.out or os.path.splitext(args.video)[0] + ".contact.json"
    with open(out, "w", encoding="utf-8") as f:
        json.dump(res, f, indent=1)
    print(json.dumps(res["summary"], indent=2))
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
