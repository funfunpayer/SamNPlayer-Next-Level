"""Trains our own detector, the student (D-155 step 5), on the label editor's
exports and writes it as one ONNX file the app runs with ONNX Runtime.

The app starts this in its own Python (set up with uv, nothing of the system
is touched) and reads one JSON object per line on stdout:

  {"event": "start", "device": "cuda", "train": 120, "valid": 30, "classes": [...]}
  {"event": "progress", "epoch": 3, "epochs": 50, "step": 40, "steps": 60}
  {"event": "epoch", "epoch": 3, "epochs": 50, "map50": 0.41}
  {"event": "done", "model": ".../student.onnx", "classes": [...], "stopped": false}
  {"event": "error", "message": "..."}

A file named STOP in the output folder ends the run after the current step;
what was learned so far is still exported.

Every input folder is one clip's export (images/, labels/ in YOLO format,
classes.txt). Whole clips go to validation, so it never sees a frame of a
training clip; with a single clip its last fifth is the validation part.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
from pathlib import Path

MODELS = {"nano": "RFDETRNano", "small": "RFDETRSmall", "medium": "RFDETRMedium"}


def say(event: str, **fields) -> None:
    print(json.dumps({"event": event, **fields}), flush=True)


def read_clip(folder: Path) -> tuple[list[str], list[tuple[Path, list[str]]]]:
    names = [n.strip() for n in (folder / "classes.txt").read_text(encoding="utf-8").splitlines() if n.strip()]
    samples = []
    for img in sorted((folder / "images").iterdir()):
        if img.suffix.lower() not in (".jpg", ".jpeg", ".png"):
            continue
        label = folder / "labels" / (img.stem + ".txt")
        lines = label.read_text(encoding="utf-8").splitlines() if label.exists() else []
        samples.append((img, [ln for ln in lines if ln.strip()]))
    return names, samples


def build_dataset(clips: list[Path], work: Path, valid_share: float = 0.2) -> tuple[list[str], int, int]:
    """One YOLO dataset (data.yaml, train/, valid/) with the clips' class
    names merged and their label ids renumbered."""
    if work.exists():
        shutil.rmtree(work)
    read = [(c, *read_clip(c)) for c in clips]
    names: list[str] = []
    for _, clip_names, _ in read:
        for n in clip_names:
            if n not in names:
                names.append(n)
    parts: dict[str, list[tuple[Path, list[str], list[str]]]] = {"train": [], "valid": []}
    if len(read) >= 2:
        n_valid = max(1, round(len(read) * valid_share))
        for i, (_, clip_names, samples) in enumerate(read):
            part = "valid" if i >= len(read) - n_valid else "train"
            parts[part] += [(img, lines, clip_names) for img, lines in samples]
    else:
        _, clip_names, samples = read[0]
        cut = max(1, int(len(samples) * (1 - valid_share)))
        parts["train"] = [(img, lines, clip_names) for img, lines in samples[:cut]]
        parts["valid"] = [(img, lines, clip_names) for img, lines in samples[cut:]]
    if not parts["train"] or not parts["valid"]:
        raise ValueError("too few samples: export more frames from the label editor")
    for part, samples in parts.items():
        (work / part / "images").mkdir(parents=True)
        (work / part / "labels").mkdir(parents=True)
        for i, (img, lines, clip_names) in enumerate(samples):
            stem = f"{i:06d}"
            shutil.copyfile(img, work / part / "images" / (stem + img.suffix.lower()))
            out = []
            for ln in lines:
                cls, *box = ln.split()
                out.append(" ".join([str(names.index(clip_names[int(cls)])), *box]))
            (work / part / "labels" / (stem + ".txt")).write_text("\n".join(out) + ("\n" if out else ""), encoding="utf-8")
    # The YAML is written by hand: names quoted, so no YAML library is needed.
    yaml = ["path: .", "train: train/images", "val: valid/images", f"nc: {len(names)}", "names:"]
    yaml += [f"  - {json.dumps(n)}" for n in names]
    (work / "data.yaml").write_text("\n".join(yaml) + "\n", encoding="utf-8")
    return names, len(parts["train"]), len(parts["valid"])


def pick_device(wanted: str) -> str:
    import torch

    if wanted != "auto":
        return wanted
    if torch.cuda.is_available():
        return "cuda"
    if getattr(torch.backends, "mps", None) and torch.backends.mps.is_available():
        return "mps"
    return "cpu"


def progress_callback(out: Path, epochs: int):
    from pytorch_lightning.callbacks import Callback

    class Progress(Callback):
        def on_train_batch_end(self, trainer, module, outputs, batch, batch_idx):
            steps = trainer.num_training_batches
            if batch_idx % max(1, int(steps) // 20 if steps else 1) == 0 or batch_idx + 1 == steps:
                say("progress", epoch=trainer.current_epoch + 1, epochs=epochs, step=batch_idx + 1, steps=int(steps or 0))
            if (out / "STOP").exists():
                trainer.should_stop = True

        def on_validation_epoch_end(self, trainer, module):
            metrics = {k: float(v) for k, v in trainer.callback_metrics.items() if hasattr(v, "item") and v.numel() == 1}
            map50 = next((v for k, v in metrics.items() if k.endswith("mAP_50") or k.endswith("map50")), None)
            say("epoch", epoch=trainer.current_epoch + 1, epochs=epochs, map50=map50)

    return Progress()


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", nargs="+", required=True, help="exported clip folders")
    ap.add_argument("--out", required=True, help="output folder (student.onnx, classes.json)")
    ap.add_argument("--model", choices=sorted(MODELS), default="nano")
    ap.add_argument("--epochs", type=int, default=50)
    ap.add_argument("--batch", type=int, default=4)
    ap.add_argument("--device", default="auto", help="auto, cuda, mps or cpu")
    args = ap.parse_args(argv)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "STOP").unlink(missing_ok=True)
    try:
        names, n_train, n_valid = build_dataset([Path(d) for d in args.data], out / "dataset")
        import rfdetr
        import rfdetr.training as training

        device = pick_device(args.device)
        say("start", device=device, train=n_train, valid=n_valid, classes=names, model=args.model)
        original = training.build_trainer

        def build_trainer(*a, **kw):  # our progress and STOP on top of RF-DETR's own callbacks
            trainer = original(*a, **kw)
            trainer.callbacks.append(progress_callback(out, args.epochs))
            return trainer

        training.build_trainer = build_trainer
        model = getattr(rfdetr, MODELS[args.model])()
        model.train(dataset_dir=str(out / "dataset"), epochs=args.epochs, batch_size=args.batch, grad_accum_steps=1,
                    output_dir=str(out / "run"), device=device, progress_bar=None)
        stopped = (out / "STOP").exists()
        model.export(output_dir=str(out / "export"))
        onnx = next((out / "export").glob("*.onnx"))
        shutil.copyfile(onnx, out / "student.onnx")
        (out / "classes.json").write_text(json.dumps({"classes": names, "model": args.model}, indent=1), encoding="utf-8")
        say("done", model=str(out / "student.onnx"), classes=names, stopped=stopped)
        return 0
    except Exception as e:  # reported to the app, which shows it
        say("error", message=f"{type(e).__name__}: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
