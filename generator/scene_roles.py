#!/usr/bin/env python3
"""scene_roles.py - what is what, what moves against what (stage 2 of
docs/SCENE_UNDERSTANDING_PLAN.md).

Two inputs, one rule set, no big model needed at run time:
- parts ("what is what"): body-part boxes from a teacher - NudeNet
  (`--nudenet`), a vlm_probe result (`--vlm`), or later our own RF-DETR
  model (multi-class, `frame_parts_fn(onnx=...)`);
- motion ("what moves"): the engine's own rhythm-grid scan
  (`SamNPlayer-cli scan-scene-map VIDEO --windows N --out scan.json`),
  cell scores at the stroke tempo per window.

Per window the rules give roles - the primary stroke target (the part that
moves most within the moving pair), the contact partner, parts to ignore -
and the scene type from the pair (mouth + tip = blowjob, hand_* = handjob,
breasts/nipples = titjob, vagina = penetration; tip = penis|glans). Output
`<clip>.scene.json` with
per-window roles and ROICandidate-shaped proposals for the app, which
remain proposals: the user applies them, or the opt-in setting "Apply AI
setup automatically" does (Owner 28 Sep; default off, everything shown).

A vlm_probe result from clip mode (`--clip N`, video input) also carries the
model's own reading of the scene (scene type, moving part, partner). It is
recorded per window as `vlm` and compared with the rules (`summary.vlm`);
it does not change the roles until that comparison has been measured.
`--truth` scores both against hand-labelled scene types.
"""

import argparse
import base64
import json
import math
import os
import sys

import cv2

# NudeNet class -> canonical part id (bodyparts.py / BODY_REGIONS.md).
NUDENET_MAP = {
    "MALE_GENITALIA_EXPOSED": "penis",
    "FEMALE_BREAST_EXPOSED": "breasts",
    "FEMALE_GENITALIA_EXPOSED": "vagina",
    "BUTTOCKS_EXPOSED": "buttocks",
    "ANUS_EXPOSED": "buttocks",
    "FACE_FEMALE": "face",
    "FACE_MALE": "face_male",
}
# Partner class → scene type. Includes BODY_REGIONS / Train IDs (nipples,
# hand_1/hand_2) and common aliases so YOLO/marks/VLM labels type the same
# way as NudeNet's breasts/hand/mouth. Class strings stay on the proposal
# (Apply / contact marks keep "nipples" vs "breasts").
PARTNERS = {
    "mouth": "blowjob", "face": "blowjob",
    "hand": "handjob", "hand_1": "handjob", "hand_2": "handjob",
    "hand_left": "handjob", "hand_right": "handjob",
    "left_hand": "handjob", "right_hand": "handjob",
    "breasts": "titjob", "nipples": "titjob",
    "vagina": "penetration", "buttocks": "penetration",
}
# Tip / shaft classes that pair against PARTNERS (glans is the Everyday tip
# soft-default; vlm_parts already maps glans→penis, other teachers may not).
TIP_CLASSES = {"penis", "glans"}
MOTION_MIN = 0.25  # part motion (0..1 of the window's hottest cell) that counts as moving


def decode_scan(scan):
    """SceneMapDTO JSON -> (cols, rows, width, height, windows); window score
    bytes (base64, cols*rows, 0..255) become floats 0..1. Of the several
    windows a scan sample yields, the longest per start sample is kept."""
    cols, rows = scan["cols"], scan["rows"]
    cands = []
    for w in scan.get("windows") or []:
        raw = w.get("score")
        if isinstance(raw, str):
            raw = base64.b64decode(raw)
        if not raw or len(raw) != cols * rows:
            continue
        cands.append({"start_ms": w["startMs"], "end_ms": w["endMs"],
                      "mid_ms": (w["startMs"] + w["endMs"]) // 2,
                      "tempo_hz": w.get("tempoHz", 0.0), "score": [v / 255.0 for v in raw]})
    cands.sort(key=lambda w: w["start_ms"])
    # A scan sample (8 s) yields several overlapping windows: cluster by
    # start and keep the longest of each cluster.
    best, first = {}, None
    for c in cands:
        if first is None or c["start_ms"] - first >= 8000:
            first = c["start_ms"]
        cur = best.get(first)
        if cur is None or (c["end_ms"] - c["start_ms"]) > (cur["end_ms"] - cur["start_ms"]):
            best[first] = c
    wins = sorted(best.values(), key=lambda w: w["mid_ms"])
    return cols, rows, scan["width"], scan["height"], wins


def part_motion(box, score, cols, rows):
    """Motion of a part: mean of the strongest third of the cells under a
    normalized box (cells whose centre lies in it; the nearest cell when the
    box is smaller than one cell). A plain mean would dilute large boxes
    (breasts) whose moving area is only part of the box."""
    x0, y0, x1, y1 = box
    vals = [score[r * cols + c] for r in range(rows) for c in range(cols)
            if x0 <= (c + 0.5) / cols <= x1 and y0 <= (r + 0.5) / rows <= y1]
    if not vals:
        c = min(cols - 1, max(0, int((x0 + x1) / 2 * cols)))
        r = min(rows - 1, max(0, int((y0 + y1) / 2 * rows)))
        vals = [score[r * cols + c]]
    vals.sort(reverse=True)
    top = vals[:max(1, math.ceil(len(vals) / 3))]
    return sum(top) / len(top)


def _center(b):
    return (b[0] + b[2]) / 2, (b[1] + b[3]) / 2


def _near(a, b, factor=1.5):
    """Boxes touch, or their centres are within factor x the larger size."""
    if a[0] <= b[2] and b[0] <= a[2] and a[1] <= b[3] and b[1] <= a[3]:
        return True
    (ax, ay), (bx, by) = _center(a), _center(b)
    size = max(a[2] - a[0], a[3] - a[1], b[2] - b[0], b[3] - b[1])
    return math.hypot(ax - bx, ay - by) <= factor * size


def nudenet_parts(dets, w, h, min_score=0.3):
    """NudeNet detections -> canonical parts. One person's two breasts
    become one "breasts" box; a female face also yields a derived "mouth"
    (lower third), since NudeNet has no mouth class."""
    parts, breasts = [], []
    for d in dets:
        cls = NUDENET_MAP.get(d.get("class"))
        if cls is None or d.get("score", 0) < min_score:
            continue
        x, y, bw, bh = d["box"]
        box = [x / w, y / h, (x + bw) / w, (y + bh) / h]
        if cls == "breasts":
            breasts.append((box, d["score"]))
            continue
        parts.append({"class": cls, "box": box, "score": d["score"], "source": "nudenet"})
        if cls == "face":
            parts.append({"class": "mouth", "box": [box[0] + (box[2] - box[0]) * 0.2,
                                                    box[1] + (box[3] - box[1]) * 0.6,
                                                    box[2] - (box[2] - box[0]) * 0.2, box[3]],
                          "score": d["score"] * 0.8, "source": "nudenet:derived"})
    used = set()
    for i, (a, sa) in enumerate(breasts):
        if i in used:
            continue
        best = None
        for j, (b, sb) in enumerate(breasts):
            if j <= i or j in used:
                continue
            dx = abs(_center(a)[0] - _center(b)[0])
            if dx < 2.5 * max(a[2] - a[0], b[2] - b[0]) and (best is None or dx < best[0]):
                best = (dx, j, b, sb)
        if best:
            _, j, b, sb = best
            used |= {i, j}
            box = [min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3])]
            parts.append({"class": "breasts", "box": box, "score": min(sa, sb), "source": "nudenet"})
        else:
            parts.append({"class": "breasts", "box": a, "score": sa, "source": "nudenet"})
    return parts


def vlm_parts(frame):
    out = []
    for b in frame.get("boxes", []):
        cls = {"glans": "penis", "thigh": "thigh", "contact": "contact"}.get(b["label"], b["label"])
        out.append({"class": cls, "box": [b["x0"], b["y0"], b["x1"], b["y1"]],
                    "score": 0.6, "source": "vlm"})
    return out


def assign_roles(parts, score, cols, rows):
    """Roles for one window. Returns dict(primary, partner, scene_type,
    confidence, ignore) - primary/partner are parts or None."""
    for p in parts:
        p["motion"] = round(part_motion(p["box"], score, cols, rows), 3)
    tips = sorted([p for p in parts if p["class"] in TIP_CLASSES], key=lambda p: -p["motion"])
    cands = [p for p in parts if p["class"] in PARTNERS]
    res = {"primary": None, "partner": None, "scene_type": None, "confidence": 0.0, "ignore": []}
    best = None
    for tip in tips:
        for pa in cands:
            if not _near(tip["box"], pa["box"]):
                continue
            pair_motion = max(tip["motion"], pa["motion"])
            if best is None or pair_motion > best[0]:
                best = (pair_motion, tip, pa)
    if best and best[0] >= MOTION_MIN:
        m, tip, pa = best
        primary, partner = (pa, tip) if pa["motion"] > tip["motion"] else (tip, pa)
        res.update(primary=primary, partner=partner, scene_type=PARTNERS[pa["class"]],
                   confidence=round(min(1.0, 0.5 + m / 2), 2))
    else:
        # Tip hidden (in the mouth / between breasts) or no pair: the
        # most-moving partner-type part decides, with lower confidence.
        moving = sorted([p for p in cands if p["motion"] >= MOTION_MIN], key=lambda p: -p["motion"])
        if moving:
            top = moving[0]
            res.update(primary=top, scene_type=PARTNERS[top["class"]],
                       confidence=round(min(0.6, 0.3 + top["motion"] / 3), 2))
    chosen = [x for x in (res["primary"], res["partner"]) if x]
    for p in parts:
        if p in chosen or p["class"] in ("contact", "mouth"):
            continue
        if not any(_near(p["box"], c["box"], 1.0) for c in chosen):
            res["ignore"].append(p)
    return res


def _roi(part, w, h, score):
    x0, y0, x1, y1 = part["box"]
    return {"x": int(round(x0 * w)), "y": int(round(y0 * h)), "w": int(round((x1 - x0) * w)),
            "h": int(round((y1 - y0) * h)), "score": round(score, 3), "class": part["class"]}


def build(video, scan, parts_at, log=print, clip_at=None):
    """parts_at(t_ms) -> list of parts for the frame nearest t_ms;
    clip_at(t_ms) -> the VLM clip-mode answer near t_ms or None."""
    cols, rows, w, h, wins = decode_scan(scan)
    windows = []
    for win in wins:
        parts = parts_at(win["mid_ms"])
        r = assign_roles(parts, win["score"], cols, rows)
        windows.append({
            "t_ms": win["mid_ms"], "start_ms": win["start_ms"], "end_ms": win["end_ms"],
            "tempo_hz": win["tempo_hz"], "scene_type": r["scene_type"], "confidence": r["confidence"],
            "primary": r["primary"], "partner": r["partner"], "ignore": r["ignore"],
            "parts": parts,
            "vlm": clip_at(win["mid_ms"]) if clip_at else None,
        })
    typed = [x for x in windows if x["scene_type"]]
    counts = {}
    for x in typed:
        counts[x["scene_type"]] = counts.get(x["scene_type"], 0) + 1
    proposals = []
    for x in windows:
        if x["primary"] is None:
            continue
        item = {"t_ms": x["t_ms"], "start_ms": x["start_ms"], "end_ms": x["end_ms"],
                "scene_type": x["scene_type"], "confidence": x["confidence"],
                "primary": _roi(x["primary"], w, h, x["confidence"])}
        if x["partner"]:
            item["partner"] = _roi(x["partner"], w, h, x["confidence"])
        proposals.append(item)
    summary = {"windows": len(windows), "typed": len(typed), "scene_types": counts}
    both = [x for x in windows if x["scene_type"] and (x["vlm"] or {}).get("scene_type")]
    if clip_at:
        summary["vlm"] = {"windows": sum(1 for x in windows if (x["vlm"] or {}).get("scene_type")),
                          "both": len(both),
                          "agree": sum(1 for x in both if x["vlm"]["scene_type"] == x["scene_type"])}
    log(f"scene roles: {len(typed)}/{len(windows)} windows typed {counts}")
    return {"version": 1, "tool": "scene_roles", "video": os.path.basename(video),
            "width": w, "height": h, "windows": windows, "proposals": proposals,
            "summary": summary}


# Only windows at least this sure become contact points: a visible pair,
# or a strongly moving part. Measured (28 Sep): multi-person clip r 0.304 ->
# 0.440, both goldens unchanged; with every typed window (0.0) the
# multi-person clip reaches 0.449 but clip_voll drops 0.752 -> 0.702 (face
# bobbing in titjob shots read as blowjob); pairs only (1.0) keep too little.
CONTACT_MIN_CONFIDENCE = 0.5


def contact_points_from_scene(res, step_ms=500, min_confidence=CONTACT_MIN_CONFIDENCE):
    """The primary (moving) part of each typed window as contact points, in
    the contact_points.py format - so Generate's --contact-points / the
    Advanced "Use contact points" option use it unchanged. The moving part
    measured better as the rhythm-grid anchor than the contact region
    itself (multi-person clip r 0.440 vs 0.401 for NudeNet contact points)."""
    pts = []
    for w in res["windows"]:
        p = w["primary"]
        if not p or w["confidence"] < min_confidence:
            continue
        x, y = _center(p["box"])
        for t in range(w["start_ms"], w["end_ms"] + 1, step_ms):
            pts.append({"t_ms": int(t), "x": round(x, 4), "y": round(y, 4),
                        "source": "scene:" + p.get("source", "?"), "rule": "moving:" + p["class"],
                        "score": w["confidence"], "box": [round(v, 4) for v in p["box"]]})
    pts.sort(key=lambda p: p["t_ms"])
    dedup = []
    for p in pts:
        if dedup and dedup[-1]["t_ms"] == p["t_ms"]:
            continue
        dedup.append(p)
    for p in dedup:
        p["agree"] = 1
    return {"version": 1, "tool": "scene_roles", "video": res["video"],
            "teachers": sorted({p["source"] for p in dedup}), "points": dedup,
            "summary": {"points": len(dedup)}}


def clip_at_fn(vlm, max_gap_ms=5000):
    """clip_at(t_ms) for build(): the clip-mode answer of the nearest
    vlm_probe keyframe within max_gap_ms, else None."""
    frames = sorted((f for f in (vlm or {}).get("frames", []) if f.get("clip")),
                    key=lambda f: f["t_ms"])

    def clip_at(t_ms):
        if not frames:
            return None
        f = min(frames, key=lambda f: abs(f["t_ms"] - t_ms))
        return dict(f["clip"], t_ms=f["t_ms"]) if abs(f["t_ms"] - t_ms) <= max_gap_ms else None
    return clip_at


def score_scene_types(res, truth, max_gap_ms=6000):
    """Scene-type accuracy of the rules and of the VLM clip answers against
    hand labels ({"scene_types": [{"t_ms", "scene_type"}]}): per labelled
    time the nearest window. Only windows that give a type count as typed."""
    out = {"labelled": 0, "rules": {"typed": 0, "right": 0}, "vlm": {"typed": 0, "right": 0}}
    wins = res["windows"]
    for lab in truth.get("scene_types", []):
        if not wins:
            break
        w = min(wins, key=lambda x: abs(x["t_ms"] - lab["t_ms"]))
        if abs(w["t_ms"] - lab["t_ms"]) > max_gap_ms:
            continue
        out["labelled"] += 1
        for key, got in (("rules", w["scene_type"]), ("vlm", (w.get("vlm") or {}).get("scene_type"))):
            if got:
                out[key]["typed"] += 1
                out[key]["right"] += got == lab["scene_type"]
    return out


def frame_parts_fn(video, detect=None, onnx=None, vlm=None):
    """parts_at(t_ms) from the chosen teachers (merged)."""
    cap = cv2.VideoCapture(video)
    w, h = cap.get(cv2.CAP_PROP_FRAME_WIDTH), cap.get(cv2.CAP_PROP_FRAME_HEIGHT)
    vlm_frames = sorted((vlm or {}).get("frames", []), key=lambda f: f["t_ms"])

    def parts_at(t_ms):
        parts = []
        if detect is not None or onnx is not None:
            cap.set(cv2.CAP_PROP_POS_MSEC, t_ms)
            ok, frame = cap.read()
            if ok and detect is not None:
                parts += nudenet_parts(detect(frame), w, h)
            if ok and onnx is not None:
                parts += onnx(frame, w, h)
        if vlm_frames:
            f = min(vlm_frames, key=lambda f: abs(f["t_ms"] - t_ms))
            if abs(f["t_ms"] - t_ms) <= 5000:
                parts += vlm_parts(f)
        return parts
    return parts_at


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--video", required=True)
    ap.add_argument("--scan", required=True, help="scan-scene-map JSON")
    ap.add_argument("--nudenet", action="store_true")
    ap.add_argument("--vlm", help="vlm_probe.py result (boxes as parts; clip mode also "
                                  "records the model's scene reading per window)")
    ap.add_argument("--truth", help="hand-labelled scene types to score against "
                                    "(e.g. testdata/vlm_labels/multi_person_642s.scene_types.json)")
    ap.add_argument("--out")
    ap.add_argument("--contact-out", help="also write the moving parts as contact points "
                                         "(default <clip>.scene.contact.json)")
    ap.add_argument("--min-confidence", type=float, default=CONTACT_MIN_CONFIDENCE,
                    help="windows below this confidence give no contact point")
    args = ap.parse_args(argv)
    if not args.nudenet and not args.vlm:
        print("no parts teacher - use --nudenet and/or --vlm", file=sys.stderr)
        return 2
    with open(args.scan, encoding="utf-8") as f:
        scan = json.load(f)
    detect = None
    if args.nudenet:
        import contact_points
        try:
            detect = contact_points.nudenet_detector()
        except RuntimeError as e:
            print(str(e), file=sys.stderr)
            return 2
    vlm = None
    if args.vlm:
        with open(args.vlm, encoding="utf-8") as f:
            vlm = json.load(f)
    res = build(args.video, scan, frame_parts_fn(args.video, detect=detect, vlm=vlm),
                clip_at=clip_at_fn(vlm) if vlm and vlm.get("clip") else None)
    if args.truth:
        with open(args.truth, encoding="utf-8") as f:
            res["summary"]["truth"] = score_scene_types(res, json.load(f))
    out = args.out or os.path.splitext(args.video)[0] + ".scene.json"
    with open(out, "w", encoding="utf-8") as f:
        json.dump(res, f, indent=1)
    cout = args.contact_out or os.path.splitext(args.video)[0] + ".scene.contact.json"
    with open(cout, "w", encoding="utf-8") as f:
        json.dump(contact_points_from_scene(res, min_confidence=args.min_confidence), f, indent=1)
    print(json.dumps(res["summary"], indent=2))
    print(f"wrote {out} and {cout} (use with generate --rhythm-grid --contact-points)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
