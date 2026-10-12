#!/usr/bin/env python3
"""contact_detector.py - our own contact detector (V3 of
docs/VLM_TEACHER_PLAN.md): confirmed marks -> RF-DETR -> ONNX.

The loop, end to end:
1. Teachers (NudeNet, VLM probes) -> `contact_points.py` -> <clip>.contact.json
2. `import-contact-candidates FILE.samn --contact ...` writes the consensus
   points as "contact" region marks, author auto, reviewed:false.
3. The user confirms/rejects them in the scene map (and can draw own
   region marks, which count as confirmed).
4. SceneMap learning export (P5c, opt-in) writes only confirmed marks to
   <learning dir>/<clip>/reviewed_yolo/.
5. `contact_detector.py dataset` merges those clips into one RF-DETR (YOLO
   layout) dataset, split by clip so validation frames come from clips the
   model never trained on.
6. `contact_detector.py train` fine-tunes RF-DETR (Apache-2.0 code and
   weights - not AGPL ultralytics) on the Owner's GPU and exports ONNX plus
   a small model card.
7. `contact_points.py --onnx contact_detector.onnx` uses the result as one
   more teacher - and the only one fast enough for every frame later.

Optional dependency, like ai_roi.py / train_yolo_model.py: training needs
`pip install "rfdetr[train]"` (PyTorch). Nothing downloads by itself here;
RF-DETR fetches its own pretrained backbone on the first `train`.
"""

import argparse
import datetime
import glob
import json
import os
import random
import shutil
import sys

SIZES = {"nano": "RFDETRNano", "small": "RFDETRSmall", "medium": "RFDETRMedium",
         "base": "RFDETRBase"}
IMG_EXT = (".png", ".jpg", ".jpeg")


# ---------------------------------------------------------------- dataset

def find_clips(learning_dirs):
    """reviewed_yolo dirs (P5c) under the given learning roots, sorted."""
    out = []
    for root in learning_dirs:
        for cj in glob.glob(os.path.join(root, "**", "reviewed_yolo", "classes.json"), recursive=True):
            out.append(os.path.dirname(cj))
    return sorted(set(out))


def _read_clip(clip_dir):
    with open(os.path.join(clip_dir, "classes.json"), encoding="utf-8") as f:
        names = json.load(f).get("names", [])
    items = []
    for img in sorted(os.listdir(os.path.join(clip_dir, "images"))):
        if not img.lower().endswith(IMG_EXT):
            continue
        lbl = os.path.join(clip_dir, "labels", os.path.splitext(img)[0] + ".txt")
        if not os.path.exists(lbl):
            continue
        rows = []
        with open(lbl, encoding="utf-8") as f:
            for line in f:
                parts = line.split()
                if len(parts) != 5:
                    continue
                cid = int(parts[0])
                if 0 <= cid < len(names):
                    rows.append((names[cid], [float(v) for v in parts[1:]]))
        if rows:
            items.append((os.path.join(clip_dir, "images", img), rows))
    return items


def build_dataset(learning_dirs, out_dir, classes=None, valid_frac=0.2, seed=7, log=print):
    """Merges P5c clips into one YOLO-layout dataset RF-DETR trains on.

    Class ids differ per clip (each clip's classes.json), so labels are
    remapped onto one global name list. With >= 3 clips the split is by
    clip; with fewer, by frame (logged as a leak risk)."""
    clips = find_clips(learning_dirs)
    if not clips:
        raise ValueError("no reviewed_yolo clips found - export confirmed marks first (P5c)")
    per_clip = {c: _read_clip(c) for c in clips}
    wanted = [c.lower() for c in classes] if classes else None
    names = sorted({n for items in per_clip.values() for _, rows in items for n, _ in rows
                    if wanted is None or n in wanted})
    if not names:
        raise ValueError("no labels of the requested classes in the confirmed marks")
    index = {n: i for i, n in enumerate(names)}

    rng = random.Random(seed)
    split = {}
    if len(clips) >= 3:
        order = clips[:]
        rng.shuffle(order)
        n_valid = max(1, int(round(len(order) * valid_frac)))
        for i, c in enumerate(order):
            split[c] = "valid" if i < n_valid else "train"
        by = "clip"
    else:
        by = "frame"
        log("only %d clip(s): splitting by frame - validation shares clips with training" % len(clips))

    if os.path.exists(out_dir):
        shutil.rmtree(out_dir)
    stats = {"train": 0, "valid": 0}
    for s in stats:
        os.makedirs(os.path.join(out_dir, s, "images"))
        os.makedirs(os.path.join(out_dir, s, "labels"))
    for ci, c in enumerate(clips):
        for k, (img, rows) in enumerate(per_clip[c]):
            rows = [(n, b) for n, b in rows if n in index]
            if not rows:
                continue
            s = split.get(c) or ("valid" if rng.random() < valid_frac else "train")
            stem = "c%03d_%s" % (ci, os.path.splitext(os.path.basename(img))[0])
            shutil.copyfile(img, os.path.join(out_dir, s, "images", stem + os.path.splitext(img)[1]))
            with open(os.path.join(out_dir, s, "labels", stem + ".txt"), "w", encoding="utf-8") as f:
                for n, b in rows:
                    f.write("%d %.6f %.6f %.6f %.6f\n" % (index[n], *b))
            stats[s] += 1
    if stats["train"] == 0 or stats["valid"] == 0:
        raise ValueError("dataset too small: %d train / %d valid images" % (stats["train"], stats["valid"]))
    with open(os.path.join(out_dir, "data.yaml"), "w", encoding="utf-8") as f:
        f.write("# written by contact_detector.py - RF-DETR YOLO layout\n")
        f.write("path: .\ntrain: train/images\nval: valid/images\n")
        f.write("nc: %d\nnames:\n" % len(names))
        for n in names:
            f.write("  - %s\n" % n)
    info = {"version": 1, "names": names, "clips": clips, "split_by": by, "images": stats}
    with open(os.path.join(out_dir, "dataset.json"), "w", encoding="utf-8") as f:
        json.dump(info, f, indent=1)
    log("dataset: %d train / %d valid images, classes %s (split by %s)" % (
        stats["train"], stats["valid"], names, by))
    return info


# ---------------------------------------------------------------- train

def _rfdetr_class(size):
    try:
        import rfdetr
    except ImportError as e:
        raise RuntimeError('RF-DETR is not installed - `pip install "rfdetr[train]"` '
                           "(PyTorch; see docs/VLM_MODELS.md)") from e
    if size not in SIZES:
        raise ValueError("size must be one of %s" % ", ".join(SIZES))
    return getattr(rfdetr, SIZES[size])


def train(dataset_dir, out_path, size="nano", epochs=50, batch=4, device="cuda",
          resolution=None, work_dir=None, log=print):
    with open(os.path.join(dataset_dir, "dataset.json"), encoding="utf-8") as f:
        info = json.load(f)
    cls = _rfdetr_class(size)
    work_dir = work_dir or os.path.splitext(out_path)[0] + "_rfdetr"
    os.makedirs(work_dir, exist_ok=True)
    kwargs = dict(dataset_dir=dataset_dir, epochs=epochs, batch_size=batch,
                  output_dir=work_dir, device=device)
    if resolution:
        kwargs["resolution"] = resolution
    model = cls()
    log("training RF-DETR %s on %s (%d train images) ..." % (size, dataset_dir, info["images"]["train"]))
    model.train(**kwargs)
    exported = str(model.export(output_dir=work_dir, format="onnx"))
    if os.path.isdir(exported):
        found = sorted(glob.glob(os.path.join(exported, "*.onnx")))
        if not found:
            raise RuntimeError("RF-DETR export wrote no .onnx into %s" % exported)
        exported = found[0]
    shutil.copyfile(exported, out_path)
    card = {
        "version": 1, "tool": "contact_detector", "arch": "rf-detr-" + size,
        "classes": info["names"], "dataset": info, "epochs": epochs,
        "licence": "RF-DETR code and pretrained weights: Apache-2.0",
        "created": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
    }
    with open(card_path(out_path), "w", encoding="utf-8") as f:
        json.dump(card, f, indent=1)
    log("wrote %s + %s" % (out_path, card_path(out_path)))
    return card


def card_path(onnx_path):
    return os.path.splitext(onnx_path)[0] + ".json"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("dataset", help="merge confirmed marks (P5c) into an RF-DETR dataset")
    d.add_argument("--learning-dir", action="append", required=True,
                   help="scene_map_learning root (repeatable)")
    d.add_argument("--out", required=True)
    d.add_argument("--classes", help="comma list, default all (e.g. contact)")
    d.add_argument("--valid-frac", type=float, default=0.2)
    t = sub.add_parser("train", help="fine-tune RF-DETR and export ONNX (needs rfdetr[train])")
    t.add_argument("--dataset", required=True)
    t.add_argument("--out", required=True, help="e.g. contact_detector.onnx")
    t.add_argument("--size", default="nano", choices=sorted(SIZES))
    t.add_argument("--epochs", type=int, default=50)
    t.add_argument("--batch", type=int, default=4)
    t.add_argument("--device", default="cuda")
    t.add_argument("--resolution", type=int)
    args = ap.parse_args(argv)
    try:
        if args.cmd == "dataset":
            build_dataset(args.learning_dir, args.out,
                          args.classes.split(",") if args.classes else None, args.valid_frac)
        else:
            train(args.dataset, args.out, args.size, args.epochs, args.batch, args.device,
                  args.resolution)
    except (RuntimeError, ValueError) as e:
        print(str(e), file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
