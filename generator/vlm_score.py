#!/usr/bin/env python3
"""vlm_score.py - scores a vlm_probe.py result against reference keyframe
labels (V0 gate, docs/VLM_TEACHER_PLAN.md).

Reference labels live next to the golden clips as `vlm_oracle.json`
(hand labels: the `contact` box where the stroke happens, `exclude` boxes for
thighs / stray hands). For each labelled keyframe the nearest probe frame
(within --max-gap-ms) is compared:

- contact hit: the probe's contact box centre lies inside the reference
  contact box (the engine only needs the right cell region, not a tight box)
- contact IoU
- contact on exclude: the probe's contact centre lies in a reference exclude
  box (the classic "thigh wins" error)

Usage:
  python3 vlm_score.py clip_voll.vlm.json \
      testdata/golden_clips/clip_voll_tftj/vlm_oracle.json
"""

import argparse
import json
import sys


def _center(b):
    return (b[0] + b[2]) / 2.0, (b[1] + b[3]) / 2.0


def _inside(pt, b):
    return b[0] <= pt[0] <= b[2] and b[1] <= pt[1] <= b[3]


def iou(a, b):
    ix = max(0.0, min(a[2], b[2]) - max(a[0], b[0]))
    iy = max(0.0, min(a[3], b[3]) - max(a[1], b[1]))
    inter = ix * iy
    union = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - inter
    return inter / union if union > 0 else 0.0


def _probe_contact(frame):
    """Largest contact box of a probe frame as [x0,y0,x1,y1], or None."""
    boxes = [b for b in frame.get("boxes", []) if b.get("label") == "contact"]
    if not boxes:
        return None
    b = max(boxes, key=lambda b: (b["x1"] - b["x0"]) * (b["y1"] - b["y0"]))
    return [b["x0"], b["y0"], b["x1"], b["y1"]]


def score(probe, oracle, max_gap_ms=2500):
    frames = [f for f in probe.get("frames", []) if f.get("status") != "no_frame"]
    # Exemplar mode showed these keyframes to the model with the answer drawn
    # in; scoring them would only test copying.
    shown = probe.get("exemplar_t_ms", [])
    rows = []
    skipped = 0
    for kf in oracle.get("keyframes", []):
        if any(abs(kf["t_ms"] - t) <= 250 for t in shown):
            skipped += 1
            continue
        near = min(frames, key=lambda f: abs(f["t_ms"] - kf["t_ms"]), default=None)
        if near is None or abs(near["t_ms"] - kf["t_ms"]) > max_gap_ms:
            rows.append({"t_ms": kf["t_ms"], "matched": False})
            continue
        pc = _probe_contact(near)
        row = {"t_ms": kf["t_ms"], "matched": True, "probe_t_ms": near["t_ms"],
               "status": near.get("status"), "has_contact": pc is not None}
        if pc is not None:
            c = _center(pc)
            row["hit"] = _inside(c, kf["contact"])
            row["iou"] = round(iou(pc, kf["contact"]), 3)
            row["on_exclude"] = any(_inside(c, e) for e in kf.get("exclude", []))
        rows.append(row)
    matched = [r for r in rows if r["matched"]]
    with_c = [r for r in matched if r["has_contact"]]
    n = len(matched)
    return {
        "keyframes": len(rows),
        "skipped_exemplars": skipped,
        "matched": n,
        "contact_rate": round(len(with_c) / n, 3) if n else None,
        # Misses include frames without any contact box: a refusal or an
        # empty answer is as useless to the engine as a wrong box.
        "hit_rate": round(sum(r["hit"] for r in with_c) / n, 3) if n else None,
        "mean_iou": round(sum(r["iou"] for r in with_c) / len(with_c), 3) if with_c else None,
        "on_exclude_rate": round(sum(r["on_exclude"] for r in with_c) / n, 3) if n else None,
        "rows": rows,
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("probe_json")
    ap.add_argument("oracle_json")
    ap.add_argument("--max-gap-ms", type=int, default=2500)
    args = ap.parse_args(argv)
    with open(args.probe_json, encoding="utf-8") as f:
        probe = json.load(f)
    with open(args.oracle_json, encoding="utf-8") as f:
        oracle = json.load(f)
    res = score(probe, oracle, args.max_gap_ms)
    summary = {k: v for k, v in res.items() if k != "rows"}
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
