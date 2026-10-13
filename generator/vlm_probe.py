#!/usr/bin/env python3
"""vlm_probe.py - asks a LOCAL vision-language model (e.g. Qwen2.5-VL via
Ollama or LM Studio) where the body regions and the stroke contact are on
keyframes of a clip, and writes the answers as JSON.

Phase V0 of docs/VLM_TEACHER_PLAN.md: "does it hold up?". The tool only
records proposals. It changes nothing in Generate, writes no marks, and no
training labels. Claude measures the JSON against the golden clips (cell hit,
end-to-end r) before anything is built on top.

Same rules as ai_roi.py / colibri_client.py (docs/AI_ADAPTER.md):
- Local only. Frames go to base_url (default: Ollama on 127.0.0.1), never to
  a cloud service. No model in the repo, no automatic download.
- No extra dependency: the HTTP call is colibri_client.chat (stdlib), frames
  come from OpenCV like everywhere else in the generator.

Coordinates: model families disagree (Qwen2.5-VL answers in pixels of the
image it was sent, Qwen3-VL in 0..1000, others in 0..1). The tool never
guesses: `--coord auto` (default) first sends a synthetic calibration image
with two known rectangles and fits the scale from the answer. Frames are
sent pre-resized to multiples of 28 (long side <= 896) so a Qwen-style
model does not resize them again.

Usage (Owner PC, e.g. `ollama pull qwen2.5vl:7b` first):
  python3 vlm_probe.py --video clip.mp4 --model qwen2.5vl:7b \
      --overlay-dir ./vlm_overlay
  -> clip.vlm.json next to the video (send that file back; it holds no
     images), overlay JPEGs for a quick visual check.
  python3 vlm_probe.py --calibrate --model qwen2.5vl:7b
  -> only checks the coordinate convention.
  python3 vlm_probe.py --video clip.mp4 --model qwen2.5vl:7b --clip 6
  -> clip mode (video): 6 frames over 3 s per keyframe; the answer adds
     scene type, moving part, partner and motion axis (scene_roles.py
     --vlm reads it).
LM Studio / Colibri / vLLM: --backend lmstudio|colibri|vllm (URL + timeout
presets, --base-url overrides) and that runtime's model id.
"""

import argparse
import base64
import json
import os
import re
import sys
import time

import cv2
import numpy as np

import bodyparts
import colibri_client

PROMPT_VERSION = "v0.2-2026-09-28"  # v0.1: exemplar mode, v0.2: clip mode
DEFAULT_BASE_URL = "http://127.0.0.1:11434"  # Ollama; LM Studio: :1234
DEFAULT_TIMEOUT_S = 180.0  # first call loads the model into VRAM

# Local runtimes that speak OpenAI /v1/chat/completions with image_url parts
# (docs/VLM_MODELS.md). Colibri streams very large MoE models (GLM-5.3-Flash,
# DeepSeek V4.1 Flash - both with vision) from disk: slow per image, fine
# for a few dozen keyframes, hence the long timeout. Its upstream README
# documents port 8000; SamNPlayer's Settings assume 8080 for the prose
# helpers - start it with the port you configure there.
BACKENDS = {
    "ollama": {"base_url": "http://127.0.0.1:11434", "timeout": 180.0},
    "lmstudio": {"base_url": "http://127.0.0.1:1234", "timeout": 180.0},
    "colibri": {"base_url": "http://127.0.0.1:8000", "timeout": 1800.0},
    "vllm": {"base_url": "http://127.0.0.1:8000", "timeout": 180.0},
}


def backend_settings(backend=None, base_url=None, timeout=None):
    """(base_url, timeout) for a named backend, explicit values winning."""
    preset = BACKENDS.get(backend or "ollama")
    if preset is None:
        raise ValueError(f"unknown backend {backend!r} - one of {', '.join(sorted(BACKENDS))}")
    return base_url or preset["base_url"], timeout or preset["timeout"]
MAX_SIDE = 896
PATCH = 28

# Probe vocabulary: the canonical regions a VLM can tell apart on one frame,
# plus the two the engine needs most - where the stroke happens (contact)
# and the classic false source (thigh). hand_1/hand_2 collapse to "hand": a
# single frame cannot tell which hand is which.
LABELS = ["contact", "penis", "glans", "vagina", "mouth", "hand",
          "breasts", "face", "thigh"]

PROMPT = (
    "You label frames for a motion-tracking tool. Return ONLY a JSON array, "
    "no prose. Each item: {\"label\": <one of " + ", ".join(LABELS) + ">, "
    "\"bbox_2d\": [x1, y1, x2, y2]}. 'contact' is the region where the main "
    "back-and-forth motion of the scene happens, where the bodies meet. "
    "List every listed region that is visible, each hand and thigh "
    "separately; omit what is not visible. Return [] if none is visible."
)

# Exemplar mode (Owner idea, 27 Sep: "tell Qwen what you marked, it should
# watch that and mark it"): reference frames of the same clip with the
# contact region drawn in GREEN and no-go regions in RED go first; the model
# finds the SAME region - same people, same body parts - in the last image.
# That removes the "which of the two people?" guess on multi-person clips.
EXEMPLAR_PROMPT = (
    "The first {n} image(s) are reference frames from the same video. In them "
    "the GREEN box marks the 'contact' region: where the main back-and-forth "
    "motion happens. RED boxes mark regions that must NOT be used (another "
    "person, hands, thighs).{follow} The LAST image is a new frame from the "
    "same video. Find the same contact region in the LAST image - the same "
    "people and the same body parts as marked, even if they moved. Return ONLY "
    "a JSON array, no prose: [{{\"label\": \"contact\", \"bbox_2d\": "
    "[x1, y1, x2, y2]}}], coordinates for the LAST image; add "
    "{{\"label\": \"exclude\", \"bbox_2d\": [...]}} for regions matching the "
    "red boxes if visible. Return [] if the contact region is not visible."
)
# Clip mode (stage 2b, docs/SCENE_UNDERSTANDING_PLAN.md): N frames spanning
# a few seconds, oldest first, in one request. The model sees the motion,
# so it can say what moves against what - one still frame cannot.
SCENE_TYPES = ["blowjob", "handjob", "titjob", "penetration", "other", "none"]
AXES = ["vertical", "horizontal", "depth"]
CLIP_PROMPT = (
    "These {n} images are consecutive frames of one video, oldest first, "
    "spanning {span:.1f} seconds. Look at what MOVES between them. Return ONLY "
    "a JSON object, no prose: {{\"scene_type\": <one of " + ", ".join(SCENE_TYPES) + ">, "
    "\"moving\": <the body part that makes the main back-and-forth motion, one "
    "of " + ", ".join(LABELS[1:]) + ">, \"partner\": <the body part it moves "
    "against, same list, or null>, \"axis\": <main motion direction in the "
    "image: " + ", ".join(AXES) + ">, \"people\": <number of people visible>, "
    "\"boxes\": [{{\"label\": <one of " + ", ".join(LABELS) + ">, \"bbox_2d\": "
    "[x1, y1, x2, y2]}}]}}. The boxes are for the LAST image: 'contact' is where "
    "the main motion happens; add the moving part and its partner."
)

FOLLOW_NOTE = (" The YELLOW box in the second-to-last image is where the "
               "contact region was found a few seconds earlier.")

CALIB_PROMPT = (
    "Return ONLY a JSON array, no prose. Give the bounding box of the red "
    "rectangle and of the blue rectangle as {\"label\": \"red\" or \"blue\", "
    "\"bbox_2d\": [x1, y1, x2, y2]}."
)

# Calibration boxes (normalised). Asymmetric on purpose: pixel, 0..1000 and
# 0..1 answers land far apart for them, so a wrong convention cannot fit.
CALIB_BOXES = {"red": (0.08, 0.12, 0.38, 0.52), "blue": (0.55, 0.40, 0.92, 0.86)}

NAMED_COORDS = ("pixel", "norm1000", "norm1")

_REFUSAL = re.compile(
    r"\b(sorry|can(?:no|')t|unable|not able|won't|inappropriate|explicit|"
    r"not (?:allowed|permitted)|against (?:my|the) (?:guidelines|policy))\b",
    re.IGNORECASE)

_ALIAS = {
    "hands": "hand", "hand_1": "hand", "hand_2": "hand", "left hand": "hand",
    "right hand": "hand", "finger": "hand", "fingers": "hand",
    "breast": "breasts", "chest": "breasts", "nipples": "breasts",
    "leg": "thigh", "legs": "thigh", "thighs": "thigh", "knee": "thigh",
    "contact point": "contact", "contact_point": "contact",
    "contact region": "contact", "contact_region": "contact",
    "penetration": "contact", "motion": "contact",
    "head": "face", "lips": "mouth",
}


def send_size(w, h, max_side=MAX_SIDE, patch=PATCH):
    """Frame size to send: aspect kept, both sides multiples of `patch`,
    long side <= max_side (never upscaled)."""
    scale = min(1.0, float(max_side) / max(w, h))
    sw = max(patch, int(w * scale / patch + 1e-6) * patch)
    sh = max(patch, int(h * scale / patch + 1e-6) * patch)
    return sw, sh


def coord_scale(mode, sent_w, sent_h):
    """(sx, sy) that turns an answer coordinate into 0..1."""
    if mode == "pixel":
        return 1.0 / sent_w, 1.0 / sent_h
    if mode == "norm1000":
        return 1.0 / 1000.0, 1.0 / 1000.0
    if mode == "norm1":
        return 1.0, 1.0
    raise ValueError(f"unknown coordinate mode: {mode}")


def normalize_label(raw):
    s = str(raw or "").strip().lower().replace("-", "_")
    s = _ALIAS.get(s, _ALIAS.get(s.replace("_", " "), s))
    if s in LABELS or s == "exclude":
        return s
    canon = bodyparts.normalize(s)
    if canon in ("hand_1", "hand_2"):
        return "hand"
    if canon in LABELS:
        return canon
    return "other"


def _extract_json(text, object_first=False):
    """First JSON array/object in a model answer (fences and prose around it
    tolerated). None if there is none. object_first: try {...} before [...]
    (clip mode answers an object that contains a boxes array)."""
    if not isinstance(text, str):
        return None
    t = re.sub(r"```(?:json)?", "", text)
    pairs = (("[", "]"), ("{", "}"))
    for opener, closer in (pairs[::-1] if object_first else pairs):
        start = t.find(opener)
        end = t.rfind(closer)
        if start < 0 or end <= start:
            continue
        try:
            return json.loads(t[start:end + 1])
        except ValueError:
            continue
    return None


def _items(obj):
    if isinstance(obj, list):
        return obj
    if isinstance(obj, dict):
        for key in ("objects", "boxes", "detections", "regions", "results"):
            if isinstance(obj.get(key), list):
                return obj[key]
        return [obj]
    return []


def parse_answer(text, sx, sy):
    """Model answer -> (status, boxes). status: ok | empty | refused |
    unparsed. Boxes are normalised 0..1, sorted corners, clipped; items
    without a usable 4-number box are dropped."""
    obj = _extract_json(text)
    if obj is None:
        if _REFUSAL.search(text or ""):
            return "refused", []
        return "unparsed", []
    boxes = []
    for it in _items(obj):
        if not isinstance(it, dict):
            continue
        raw_label = it.get("label", it.get("name", it.get("class", "")))
        bb = it.get("bbox_2d", it.get("bbox", it.get("box")))
        if not isinstance(bb, (list, tuple)) or len(bb) != 4:
            continue
        try:
            x0, y0, x1, y1 = (float(v) for v in bb)
        except (TypeError, ValueError):
            continue
        x0, x1 = sorted((x0 * sx, x1 * sx))
        y0, y1 = sorted((y0 * sy, y1 * sy))
        x0, x1 = max(0.0, x0), min(1.0, x1)
        y0, y1 = max(0.0, y0), min(1.0, y1)
        if x1 - x0 <= 1e-4 or y1 - y0 <= 1e-4:
            continue
        boxes.append({"label": normalize_label(raw_label), "raw_label": str(raw_label),
                      "x0": round(x0, 4), "y0": round(y0, 4),
                      "x1": round(x1, 4), "y1": round(y1, 4)})
    return ("ok" if boxes else "empty"), boxes


def clip_times_ms(t_ms, n, span_ms):
    """n frame times ending at t_ms, evenly over span_ms, oldest first
    (clamped at 0 and de-duplicated)."""
    if n < 2:
        return [t_ms]
    out = []
    for k in range(n):
        t = max(0, int(round(t_ms - span_ms + k * span_ms / (n - 1))))
        if not out or t > out[-1]:
            out.append(t)
    return out


def parse_clip_answer(text, sx, sy):
    """Clip-mode answer -> (status, boxes, clip). clip holds scene_type,
    moving, partner, axis, people - unknown values become None."""
    status, boxes = parse_answer(text, sx, sy)
    obj = _extract_json(text, object_first=True)
    if isinstance(obj, list):  # boxes only, no roles
        obj = {}
    if not isinstance(obj, dict):
        return status, boxes, None

    def pick(v, allowed):
        v = str(v).strip().lower().replace(" ", "") if v is not None else ""
        return v if v in allowed else None

    part = lambda v: (lambda n: n if n in LABELS[1:] else None)(normalize_label(v)) if v else None
    clip = {"scene_type": pick(obj.get("scene_type"), SCENE_TYPES),
            "moving": part(obj.get("moving")), "partner": part(obj.get("partner")),
            "axis": pick(obj.get("axis"), AXES)}
    try:
        clip["people"] = int(obj.get("people"))
    except (TypeError, ValueError):
        clip["people"] = None
    if status in ("empty", "unparsed") and clip["scene_type"]:
        status = "ok"
    return status, boxes, clip


def _data_url(img_bgr):
    ok, buf = cv2.imencode(".jpg", img_bgr, [cv2.IMWRITE_JPEG_QUALITY, 90])
    if not ok:
        raise RuntimeError("JPEG encode failed")
    return "data:image/jpeg;base64," + base64.b64encode(buf.tobytes()).decode("ascii")


def ask(img_bgr, prompt, model, base_url, timeout, post_fn=None):
    """Image(s) + prompt -> (answer text, latency ms). img_bgr is one image
    or a list of images (exemplar mode: references first, query last)."""
    imgs = img_bgr if isinstance(img_bgr, (list, tuple)) else [img_bgr]
    content = [{"type": "text", "text": prompt}]
    content += [{"type": "image_url", "image_url": {"url": _data_url(im)}} for im in imgs]
    messages = [{"role": "user", "content": content}]
    t0 = time.monotonic()
    text = colibri_client.chat(messages, base_url=base_url, model=model,
                               timeout=timeout, _post_fn=post_fn)
    return text, int(round((time.monotonic() - t0) * 1000))


def calibration_image(w=MAX_SIDE, h=504):
    img = np.full((h, w, 3), 235, np.uint8)
    colors = {"red": (40, 40, 220), "blue": (220, 60, 40)}  # BGR
    for name, (x0, y0, x1, y1) in CALIB_BOXES.items():
        cv2.rectangle(img, (int(x0 * w), int(y0 * h)), (int(x1 * w), int(y1 * h)),
                      colors[name], thickness=-1)
    return img


def fit_calibration(text, sent_w, sent_h):
    """Fits the answer->0..1 scale from a calibration answer. Returns a dict
    with sx, sy, the nearest named mode, and the mean box error (0..1
    units); ok is False when the answer cannot be fitted or the fit is
    poor (the convention is then unknown - send the JSON to Claude)."""
    obj = _extract_json(text)
    got = {}
    for it in _items(obj) if obj is not None else []:
        if not isinstance(it, dict):
            continue
        name = str(it.get("label", it.get("name", ""))).strip().lower()
        bb = it.get("bbox_2d", it.get("bbox", it.get("box")))
        if name in CALIB_BOXES and isinstance(bb, (list, tuple)) and len(bb) == 4:
            try:
                got[name] = [float(v) for v in bb]
            except (TypeError, ValueError):
                pass
    if len(got) < 2:
        return {"ok": False, "reason": "calibration answer lacks red/blue boxes",
                "raw": (text or "")[:2000]}
    ax, tx, ay, ty = [], [], [], []
    for name, bb in got.items():
        x0, x1 = sorted((bb[0], bb[2]))
        y0, y1 = sorted((bb[1], bb[3]))
        t = CALIB_BOXES[name]
        ax += [x0, x1]
        tx += [t[0], t[2]]
        ay += [y0, y1]
        ty += [t[1], t[3]]
    ax, tx, ay, ty = map(np.asarray, (ax, tx, ay, ty))
    if not ax.any() or not ay.any():
        return {"ok": False, "reason": "degenerate calibration answer",
                "raw": (text or "")[:2000]}
    # Least squares through the origin: truth = s * answer, per axis.
    sx = float(ax @ tx / (ax @ ax))
    sy = float(ay @ ty / (ay @ ay))
    err = float(np.mean(np.abs(np.concatenate([ax * sx - tx, ay * sy - ty]))))
    nearest = min(NAMED_COORDS, key=lambda m: sum(
        abs(np.log(s / r)) for s, r in zip((sx, sy), coord_scale(m, sent_w, sent_h))))
    return {"ok": err <= 0.05, "sx": sx, "sy": sy, "nearest_mode": nearest,
            "mean_err": round(err, 4), "raw": (text or "")[:2000]}


def calibrate(model, base_url, timeout, post_fn=None, w=MAX_SIDE, h=504):
    """Calibrates at the size the frames will be sent at, so the fitted
    scale applies to them directly whatever the model's convention."""
    img = calibration_image(w, h)
    text, latency = ask(img, CALIB_PROMPT, model, base_url, timeout, post_fn)
    res = fit_calibration(text, w, h)
    res["latency_ms"] = latency
    return res


def keyframe_times_ms(duration_ms, every_s, scenes_ms=()):
    """Mid-point of each shot plus one frame every `every_s` seconds,
    sorted, de-duplicated to 250 ms."""
    times = set()
    if every_s and every_s > 0:
        step = int(every_s * 1000)
        t = step // 2
        while t < duration_ms:
            times.add(t)
            t += step
    for start, end in scenes_ms:
        times.add((start + end) // 2)
    out = []
    for t in sorted(times):
        if 0 <= t < duration_ms and (not out or t - out[-1] >= 250):
            out.append(int(t))
    return out


def _scenes_ms(video, fps):
    import generate_funscript  # heavy import, only when scenes are wanted
    return [(int(s * 1000 / fps), int(e * 1000 / fps))
            for s, e in generate_funscript.detect_scene_boundaries(video)]


def draw_overlay(img, boxes):
    out = img.copy()
    h, w = out.shape[:2]
    for b in boxes:
        color = (0, 220, 0) if b["label"] == "contact" else (
            (0, 0, 230) if b["label"] in ("thigh", "hand") else (230, 180, 0))
        p0 = (int(b["x0"] * w), int(b["y0"] * h))
        p1 = (int(b["x1"] * w), int(b["y1"] * h))
        cv2.rectangle(out, p0, p1, color, 2)
        cv2.putText(out, b["label"], (p0[0] + 3, p0[1] + 16),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 1, cv2.LINE_AA)
    return out


def load_exemplars(path=None, specs=(), count=1):
    """Reference marks for exemplar mode as [{t_ms, contact, exclude}]
    (normalised 0..1 boxes). From a vlm_oracle.json-style file (the first
    `count` keyframes that have a contact box) and/or CLI specs
    "t_ms:x0,y0,x1,y1" (contact only)."""
    out = []
    if path:
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
        for kf in doc.get("keyframes", []):
            if kf.get("contact") and len(out) < count:
                out.append({"t_ms": int(kf["t_ms"]), "contact": list(kf["contact"]),
                            "exclude": [list(e) for e in kf.get("exclude", [])]})
    for spec in specs:
        t, box = spec.split(":", 1)
        b = [float(v) for v in box.split(",")]
        if len(b) != 4 or not (0 <= b[0] < b[2] <= 1 and 0 <= b[1] < b[3] <= 1):
            raise ValueError(f"bad exemplar box {spec!r}: want t_ms:x0,y0,x1,y1 in 0..1")
        out.append({"t_ms": int(t), "contact": b, "exclude": []})
    return sorted(out, key=lambda e: e["t_ms"])


def draw_reference(img, contact=None, excludes=(), color=(0, 220, 0)):
    """Reference image for exemplar mode: contact box (green, or yellow for
    the follow frame) and no-go boxes (red), thick enough to survive JPEG."""
    out = img.copy()
    h, w = out.shape[:2]
    t = max(2, int(round(min(w, h) / 120)))
    for b, c in [(e, (0, 0, 230)) for e in excludes] + ([(contact, color)] if contact else []):
        cv2.rectangle(out, (int(b[0] * w), int(b[1] * h)), (int(b[2] * w), int(b[3] * h)), c, t)
    return out


def probe_video(video, model, base_url=DEFAULT_BASE_URL, every_s=5.0, scenes=True,
                coord="auto", timeout=DEFAULT_TIMEOUT_S, overlay_dir=None,
                max_keyframes=0, post_fn=None, log=print, exemplars=(), follow=False,
                clip=0, clip_span_s=3.0):
    if clip >= 2 and exemplars:
        raise ValueError("clip mode and exemplar mode cannot be combined yet")
    cap = cv2.VideoCapture(video)
    if not cap.isOpened():
        raise RuntimeError(f"Could not open video: {video}")
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    n = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)
    vw = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    vh = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    duration_ms = int(n * 1000 / fps)
    sw, sh = send_size(vw, vh)

    calib = None
    if coord == "auto":
        calib = calibrate(model, base_url, timeout, post_fn, sw, sh)
        if not calib["ok"]:
            cap.release()
            raise RuntimeError("coordinate calibration failed: " + calib.get(
                "reason", f"mean error {calib.get('mean_err')}") +
                " - rerun with --calibrate and send the output to Claude")
        sx, sy = calib["sx"], calib["sy"]
        log(f"calibration: nearest={calib['nearest_mode']} err={calib['mean_err']}")
    else:
        sx, sy = coord_scale(coord, sw, sh)

    scenes_ms = _scenes_ms(video, fps) if scenes else []
    times = keyframe_times_ms(duration_ms, every_s, scenes_ms)
    refs = []
    for ex in exemplars:
        cap.set(cv2.CAP_PROP_POS_MSEC, ex["t_ms"])
        ok, frame = cap.read()
        if not ok:
            cap.release()
            raise RuntimeError(f"exemplar frame at {ex['t_ms']} ms could not be read")
        refs.append(draw_reference(cv2.resize(frame, (sw, sh), interpolation=cv2.INTER_AREA),
                                   ex["contact"], ex["exclude"]))
    ex_times = [e["t_ms"] for e in exemplars]
    # Asking about the reference frame itself would only test copying.
    times = [t for t in times if all(abs(t - e) > 250 for e in ex_times)]
    if max_keyframes and len(times) > max_keyframes:
        times = times[:max_keyframes]
    if overlay_dir:
        os.makedirs(overlay_dir, exist_ok=True)

    frames = []
    prev = None  # (image, contact box) of the last frame with a contact answer
    for i, t in enumerate(times):
        cap.set(cv2.CAP_PROP_POS_MSEC, t)
        ok, frame = cap.read()
        if not ok:
            frames.append({"t_ms": t, "status": "no_frame", "boxes": []})
            continue
        img = cv2.resize(frame, (sw, sh), interpolation=cv2.INTER_AREA)
        clip_ms = []
        if clip >= 2:
            clip_ms = clip_times_ms(t, clip, int(clip_span_s * 1000))
            imgs = []
            for ct in clip_ms[:-1]:
                cap.set(cv2.CAP_PROP_POS_MSEC, ct)
                ok, f = cap.read()
                if ok:
                    imgs.append(cv2.resize(f, (sw, sh), interpolation=cv2.INTER_AREA))
            imgs.append(img)
            prompt = CLIP_PROMPT.format(n=len(imgs), span=(clip_ms[-1] - clip_ms[0]) / 1000)
        elif refs:
            imgs = list(refs)
            if follow and prev is not None:
                imgs.append(draw_reference(prev[0], prev[1], color=(0, 220, 230)))
            prompt = EXEMPLAR_PROMPT.format(
                n=len(imgs), follow=FOLLOW_NOTE if len(imgs) > len(refs) else "")
            imgs.append(img)
        else:
            imgs, prompt = img, PROMPT
        clip_ans = None
        try:
            text, latency = ask(imgs, prompt, model, base_url, timeout, post_fn)
            if clip >= 2:
                status, boxes, clip_ans = parse_clip_answer(text, sx, sy)
            else:
                status, boxes = parse_answer(text, sx, sy)
        except Exception as e:  # noqa: BLE001 - record and keep going
            text, latency, status, boxes = str(e), 0, "error", []
        rec = {"t_ms": t, "latency_ms": latency, "status": status,
               "boxes": boxes, "raw": (text or "")[:2000]}
        if clip >= 2:
            rec["clip_ms"] = clip_ms
            rec["clip"] = clip_ans
        frames.append(rec)
        contact = [b for b in boxes if b["label"] == "contact"]
        if contact:
            c = max(contact, key=lambda b: (b["x1"] - b["x0"]) * (b["y1"] - b["y0"]))
            prev = (img, [c["x0"], c["y0"], c["x1"], c["y1"]])
        if overlay_dir and boxes:
            cv2.imwrite(os.path.join(overlay_dir, f"{t:08d}.jpg"), draw_overlay(img, boxes))
        log(f"[{i + 1}/{len(times)}] {t / 1000:7.1f}s {status:8s} "
            f"{len(boxes)} boxes {latency} ms")
    cap.release()

    return {
        "tool": "vlm_probe", "prompt_version": PROMPT_VERSION,
        "prompt": CLIP_PROMPT if clip >= 2 else (EXEMPLAR_PROMPT if refs else PROMPT),
        "labels": LABELS, "clip": clip if clip >= 2 else 0,
        "clip_span_s": clip_span_s if clip >= 2 else None,
        "exemplars": list(exemplars), "exemplar_t_ms": ex_times, "follow": bool(follow and refs),
        "model": model, "base_url": base_url,
        "video": os.path.basename(video), "fps": fps, "frame_count": n,
        "video_w": vw, "video_h": vh, "sent_w": sw, "sent_h": sh,
        "coord": coord, "scale": [sx, sy], "calibration": calib,
        "every_s": every_s, "scenes_ms": scenes_ms,
        "frames": frames, "summary": summarize(frames),
    }


def summarize(frames):
    status = {}
    labels = {}
    lat = []
    for f in frames:
        status[f["status"]] = status.get(f["status"], 0) + 1
        for b in f.get("boxes", []):
            labels[b["label"]] = labels.get(b["label"], 0) + 1
        if f.get("latency_ms"):
            lat.append(f["latency_ms"])
    asked = sum(v for k, v in status.items() if k != "no_frame")
    scene = {}
    for f in frames:
        st = (f.get("clip") or {}).get("scene_type")
        if st:
            scene[st] = scene.get(st, 0) + 1
    return {
        "keyframes": len(frames), "status": status, "labels": labels,
        "refusal_rate": round(status.get("refused", 0) / asked, 3) if asked else None,
        "contact_rate": round(sum(1 for f in frames if any(
            b["label"] == "contact" for b in f.get("boxes", []))) / asked, 3) if asked else None,
        "latency_ms_median": int(np.median(lat)) if lat else None,
        **({"scene_types": scene} if scene else {}),
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--video")
    ap.add_argument("--model", required=True, help="e.g. qwen2.5vl:7b (Ollama)")
    ap.add_argument("--backend", default="ollama", choices=sorted(BACKENDS),
                    help="runtime preset for URL/timeout (ollama, lmstudio, colibri, vllm)")
    ap.add_argument("--base-url", help="overrides the backend's URL")
    ap.add_argument("--every-s", type=float, default=5.0)
    ap.add_argument("--no-scenes", action="store_true", help="skip shot mid-points")
    ap.add_argument("--coord", default="auto", choices=("auto",) + NAMED_COORDS)
    ap.add_argument("--timeout", type=float, help="seconds per request (backend default)")
    ap.add_argument("--max-keyframes", type=int, default=0)
    ap.add_argument("--overlay-dir")
    ap.add_argument("--out")
    ap.add_argument("--exemplar-json",
                    help="reference marks (vlm_oracle.json format); exemplar mode")
    ap.add_argument("--exemplar-count", type=int, default=1,
                    help="how many keyframes of --exemplar-json to show (default 1)")
    ap.add_argument("--exemplar", action="append", default=[],
                    help="reference contact box t_ms:x0,y0,x1,y1 (0..1), repeatable")
    ap.add_argument("--follow", action="store_true",
                    help="exemplar mode: also show the last answer as a yellow box")
    ap.add_argument("--clip", type=int, default=0,
                    help="clip mode: send N frames (>= 2) ending at each keyframe as one "
                         "sequence and ask what moves against what + scene type")
    ap.add_argument("--clip-span-s", type=float, default=3.0,
                    help="clip mode: seconds the N frames span (default 3)")
    ap.add_argument("--calibrate", action="store_true",
                    help="only check the model's coordinate convention")
    args = ap.parse_args(argv)

    args.base_url, args.timeout = backend_settings(args.backend, args.base_url, args.timeout)
    if not colibri_client.available(args.base_url):
        print(f"No OpenAI-compatible server at {args.base_url} "
              "(start Ollama / LM Studio first).", file=sys.stderr)
        return 2
    if args.calibrate:
        res = calibrate(args.model, args.base_url, args.timeout)
        print(json.dumps(res, indent=2))
        return 0 if res["ok"] else 1
    if not args.video:
        ap.error("--video is required unless --calibrate")

    exemplars = load_exemplars(args.exemplar_json, args.exemplar, args.exemplar_count)
    if args.follow and not exemplars:
        ap.error("--follow needs --exemplar-json or --exemplar")
    if args.clip >= 2 and exemplars:
        ap.error("--clip cannot be combined with exemplar mode yet")
    res = probe_video(args.video, args.model, args.base_url, args.every_s,
                      not args.no_scenes, args.coord, args.timeout,
                      args.overlay_dir, args.max_keyframes,
                      exemplars=exemplars, follow=args.follow,
                      clip=args.clip, clip_span_s=args.clip_span_s)
    out = args.out or os.path.splitext(args.video)[0] + ".vlm.json"
    with open(out, "w", encoding="utf-8") as f:
        json.dump(res, f, indent=1)
    print(json.dumps(res["summary"], indent=2))
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
