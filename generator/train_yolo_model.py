#!/usr/bin/env python3
"""train_yolo_model.py - trainiert ein YOLO-Modell auf einem mit
bootstrap_yolo_dataset.py gesammelten (und idealerweise von Hand
kontrollierten) Datensatz, und exportiert es direkt im Vertrag, den
ai_roi.py erwartet.

Schließt die Lücke zwischen den beiden bisherigen Werkzeugen:
bootstrap_yolo_dataset.py baut den Datensatz, export_yolo_onnx.py
normalisiert ein FERTIG trainiertes .pt - das eigentliche `yolo detect
train` dazwischen musste bisher von Hand auf der Kommandozeile laufen.
Dieses Skript ruft beides hintereinander auf, damit "Video(s) rein, Modell
raus" ein einziger Schritt ist (Chat, 16. September 2026: "brauchen wir
Trainingssystem fest drin").

Gerätewahl (Accelerator-Strategie, docs/ENGINE.md Phase 7):
  --device auto|cuda|directml|mps|cpu  (Standard: auto)
  auto wählt der Reihe nach CUDA → MPS → DirectML → CPU.
  DirectML braucht unter Windows zusätzlich torch-directml
  (optional, nicht in requirements-ai-train.txt, weil Linux/macOS
  es nicht brauchen). CUDA bleibt auf NVIDIA die schnellste Option;
  DirectML deckt AMD/Intel unter Windows ohne CUDA-Zwang ab.

ultralytics gibt während des Trainings selbst laufend Fortschritt aus
(Epoche für Epoche, Verlustwerte) - dieses Skript leitet das unverändert
weiter (kein eigenes PROGRESS-Format), die GUI zeigt es als Rohtext-Log an,
genau wie beim Bench-Tab.

Nutzung:
  python3 train_yolo_model.py --dataset-dir ./yolo_dataset \
      --output roi_detector.onnx --epochs 100 --device auto
  python3 train_yolo_model.py --list-devices
"""

import argparse
import json
import os
import shutil
import sys


DEVICE_CHOICES = ("auto", "cuda", "directml", "mps", "cpu")

_IMAGE_EXTS = (".jpg", ".jpeg", ".png", ".webp")

# Ultralytics needs ≥1 image on each of train and val. One labeled frame is
# enough to start (val gets a copy); zero images is a hard stop with a clear
# message instead of AssertionError deep inside the dataloader.
MIN_TRAINING_IMAGES = 1


def _list_split_images(img_dir):
    if not os.path.isdir(img_dir):
        return []
    return [
        name for name in sorted(os.listdir(img_dir))
        if name.lower().endswith(_IMAGE_EXTS)
    ]


def ensure_nonempty_splits(dataset_dir):
    """Copy image+label into an empty train/val split so Ultralytics can build
    both dataloaders. Same behavior as bootstrap_yolo_dataset.ensure_nonempty_splits
    (duplicated here to avoid importing OpenCV via bootstrap when only training).

    Returns (train_count, val_count) after healing.
    """
    img_train = os.path.join(dataset_dir, "images", "train")
    img_val = os.path.join(dataset_dir, "images", "val")
    lbl_train = os.path.join(dataset_dir, "labels", "train")
    lbl_val = os.path.join(dataset_dir, "labels", "val")
    for d in (img_train, img_val, lbl_train, lbl_val):
        os.makedirs(d, exist_ok=True)

    train_imgs = _list_split_images(img_train)
    val_imgs = _list_split_images(img_val)

    def _copy_one(src_img_dir, src_lbl_dir, dst_img_dir, dst_lbl_dir, name):
        src_img = os.path.join(src_img_dir, name)
        dst_img = os.path.join(dst_img_dir, name)
        if not os.path.exists(dst_img):
            shutil.copy2(src_img, dst_img)
        stem, _ext = os.path.splitext(name)
        src_lbl = os.path.join(src_lbl_dir, stem + ".txt")
        dst_lbl = os.path.join(dst_lbl_dir, stem + ".txt")
        if os.path.isfile(src_lbl) and not os.path.exists(dst_lbl):
            shutil.copy2(src_lbl, dst_lbl)

    if not val_imgs and train_imgs:
        _copy_one(img_train, lbl_train, img_val, lbl_val, train_imgs[0])
        print(
            f"Hinweis: images/val was empty — copied {train_imgs[0]} from train "
            "so Ultralytics validation can run",
            file=sys.stderr,
        )
    elif not train_imgs and val_imgs:
        _copy_one(img_val, lbl_val, img_train, lbl_train, val_imgs[0])
        print(
            f"Hinweis: images/train was empty — copied {val_imgs[0]} from val",
            file=sys.stderr,
        )

    return (
        len(_list_split_images(img_train)),
        len(_list_split_images(img_val)),
    )


def prepare_dataset_for_training(dataset_dir):
    """Heal empty splits and fail fast with a user-facing message when the
    dataset has no labeled images. Call before ultralytics model.train().

    Returns (train_count, val_count).
    """
    train_n, val_n = ensure_nonempty_splits(dataset_dir)
    total = train_n + val_n
    # After ensure, a single unique image appears in both splits (copied), so
    # total counts the duplicate; unique ≈ max(train_n, val_n) when one was empty.
    unique = max(train_n, val_n)
    if unique < MIN_TRAINING_IMAGES or train_n < 1 or val_n < 1:
        raise RuntimeError(
            f"Training dataset is empty or still incomplete under {dataset_dir} "
            f"(train={train_n}, val={val_n}). Need at least {MIN_TRAINING_IMAGES} "
            "labeled image(s). In AI Train: mark region(s), click “Use for training”, "
            "keep at least one sample in the review list, then start training again."
        )
    if unique < 5:
        print(
            f"Hinweis: only {unique} unique labeled image(s) — training will run, "
            "but results will be weak; add more clips/stills if detection is poor.",
            file=sys.stderr,
        )
    return train_n, val_n


def available():
    """True, wenn ultralytics installiert ist. Reine Prüffunktion, löst
    nichts aus - die GUI nutzt sie, um den Training-Knopf zu aktivieren/
    auszublenden, statt ihn anzubieten und dann mit einem rohen
    ModuleNotFoundError-Traceback (Temp-Skriptpfad, kein Hinweis auf den
    fehlenden pip install) scheitern zu lassen. Siehe ai_roi.available()
    für dasselbe Muster bei der kleineren onnxruntime-Abhängigkeit."""
    try:
        import ultralytics  # noqa: F401
    except ImportError:
        return False
    return True


def _cuda_available():
    try:
        import torch
        return bool(torch.cuda.is_available())
    except Exception:
        return False


def _mps_available():
    try:
        import torch
        return bool(getattr(torch.backends, "mps", None)
                    and torch.backends.mps.is_available())
    except Exception:
        return False


def _directml_device():
    """torch.device für DirectML, oder None wenn nicht nutzbar."""
    try:
        import torch_directml
        if torch_directml.device_count() <= 0:
            return None
        return torch_directml.device()
    except Exception:
        return None


def list_devices():
    """Liste der wählbaren Geräte inkl. Verfügbarkeit - für GUI/CLI."""
    cuda_ok = _cuda_available()
    mps_ok = _mps_available()
    dml_ok = _directml_device() is not None
    return [
        {"id": "auto", "label": "Automatic (best available)", "available": True},
        {"id": "cuda", "label": "NVIDIA CUDA", "available": cuda_ok},
        {"id": "directml", "label": "DirectML (Windows, AMD/Intel/NVIDIA)", "available": dml_ok},
        {"id": "mps", "label": "Apple MPS", "available": mps_ok},
        {"id": "cpu", "label": "CPU (very slow)", "available": True},
    ]


def resolve_device(requested="auto"):
    """Mappt GUI/CLI-Wahl auf das, was ultralytics model.train(device=…)
    erwartet. Wirft RuntimeError mit klarer Meldung, wenn eine explizite
    Wahl nicht verfügbar ist (statt still auf CPU zu fallen - sonst denkt
    man, DirectML/CUDA laufe, obwohl es die CPU ist)."""
    req = (requested or "auto").strip().lower()
    if req in ("", "auto"):
        if _cuda_available():
            print("Device auto → cuda", file=sys.stderr)
            return "0"
        if _mps_available():
            print("Device auto → mps", file=sys.stderr)
            return "mps"
        dml = _directml_device()
        if dml is not None:
            print("Device auto → directml", file=sys.stderr)
            return dml
        print("Device auto → cpu (no GPU backend found)", file=sys.stderr)
        return "cpu"

    if req in ("cuda", "gpu"):
        if not _cuda_available():
            raise RuntimeError(
                "CUDA was selected but is not available "
                "(no NVIDIA driver / no torch with CUDA). "
                "On Windows without NVIDIA: --device directml or auto.")
        return "0"

    if req == "mps":
        if not _mps_available():
            raise RuntimeError(
                "MPS was selected but is not available "
                "(Apple Silicon with matching PyTorch only).")
        return "mps"

    if req in ("directml", "dml"):
        dml = _directml_device()
        if dml is None:
            raise RuntimeError(
                "DirectML was selected but is not available. "
                "On Windows: pip install torch-directml "
                "(in addition to requirements-ai-train.txt).")
        return dml

    if req == "cpu":
        return "cpu"

    # Numerische GPU-Indexe und andere ultralytics-kompatible Werte
    # unverändert durchreichen (z.B. "0", "1", "0,1").
    return requested


def train_and_export(dataset_dir, output_path, epochs=100, device="auto",
                      imgsz=640, base_model="yolov8n.pt", project_dir=None,
                      run_name="samnplayer_roi"):
    """Trainiert base_model auf dataset_dir/data.yaml und exportiert das
    Ergebnis als normalisiertes ONNX nach output_path. Wirft, statt ein
    kaputtes/fehlendes Modell stillschweigend als Erfolg zu melden - der
    Aufrufer (GUI) soll einen echten Fehler sehen, keinen leeren Erfolg."""
    try:
        from ultralytics import YOLO
    except ImportError as exc:
        # Die GUI prüft das vorher per available()/--check und sperrt den
        # Knopf entsprechend - dieser Zweig ist nur für den direkten
        # CLI-Aufruf ohne diese Prüfung (z.B. von Hand auf der
        # Kommandozeile), damit auch der einen klaren Hinweis statt eines
        # rohen ModuleNotFoundError-Tracebacks bekommt.
        raise RuntimeError(
            "ultralytics is not installed — required for training "
            "(not part of the base install; see requirements-ai-train.txt): "
            "pip install -r generator/requirements-ai-train.txt") from exc

    data_yaml = os.path.join(dataset_dir, "data.yaml")
    if not os.path.isfile(data_yaml):
        raise RuntimeError(
            f"No data.yaml under {dataset_dir}. Collect samples first: in the AI Train "
            "tab mark region(s) and click “Use for training” (or run "
            "bootstrap_yolo_dataset.py), then start training again.")

    prepare_dataset_for_training(dataset_dir)

    resolved = resolve_device(device)
    project_dir = project_dir or os.path.join(dataset_dir, "runs")
    print(f"Training: {base_model} on {data_yaml}, {epochs} epochs, "
          f"device={device!r} → {resolved!r}",
          file=sys.stderr)
    model = YOLO(base_model)
    # batch=-1 lässt ultralytics die Batchgröße automatisch an die freie
    # GPU-Speichergröße anpassen (Ziel ~60% Auslastung), statt den festen
    # Standardwert (16) zu erzwingen. Ohne das läuft eine Karte mit wenig
    # VRAM (z.B. eine GTX 1650 mit 4GB) beim Training leicht in ein "CUDA
    # out of memory", obwohl yolov8n (der Standard hier) mit kleinerer
    # Batchgröße problemlos passen würde. Auf der CPU wirkungslos (fällt
    # dort automatisch auf den festen Standardwert zurück, siehe
    # ultralytics.utils.autobatch.autobatch).
    model.train(data=data_yaml, epochs=epochs, device=resolved, imgsz=imgsz,
                batch=-1, project=project_dir, name=run_name, exist_ok=True)

    weights_path = os.path.join(project_dir, run_name, "weights", "best.pt")
    if not os.path.isfile(weights_path):
        raise RuntimeError(
            f"Training finished but no best.pt under {weights_path} — "
            "training likely failed; see the log above.")

    import export_yolo_onnx
    print(f"Exporting {weights_path} -> {output_path}...", file=sys.stderr)
    export_yolo_onnx.export_and_normalize(weights_path, output_path, imgsz=imgsz)
    if not export_yolo_onnx.verify(output_path, imgsz):
        print("WARNING: verification of the exported model failed unexpectedly — "
              "check manually before using it in production.", file=sys.stderr)
    # Keep classes.json next to the .onnx so inference can resolve
    # --preferred-classes names without pointing at the full dataset dir.
    import shutil
    classes_src = os.path.join(dataset_dir, "classes.json")
    if os.path.isfile(classes_src):
        classes_dst = os.path.join(os.path.dirname(os.path.abspath(output_path)), "classes.json")
        try:
            shutil.copy2(classes_src, classes_dst)
            print(f"classes.json -> {classes_dst}", file=sys.stderr)
        except OSError as exc:
            print(f"Note: could not copy classes.json: {exc}", file=sys.stderr)
    print(f"Done: {output_path}", file=sys.stderr)
    return output_path


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dataset-dir", help="Von bootstrap_yolo_dataset.py angelegter Ordner "
                                          "(enthält data.yaml) - nicht nötig mit --check/--list-devices")
    ap.add_argument("--output", help="Zielpfad für die fertige .onnx-Datei - nicht nötig mit --check/--list-devices")
    ap.add_argument("--epochs", type=int, default=100)
    ap.add_argument("--device", default="auto",
                     help="auto|cuda|directml|mps|cpu (oder GPU-Index wie '0') - Standard: auto")
    ap.add_argument("--imgsz", type=int, default=640)
    ap.add_argument("--base-model", default="yolov8n.pt",
                     help="Ultralytics-Basismodell - 'n' (nano) ist der kleinste/schnellste, "
                          "reicht für ein einzelnes ROI-Erkennungsproblem üblicherweise aus")
    ap.add_argument("--check", action="store_true",
                     help="Nur prüfen, ob Training grundsätzlich möglich ist (ultralytics "
                          "installiert), ohne etwas zu trainieren - für die GUI, um den "
                          "Training-Knopf zu aktivieren/auszublenden.")
    ap.add_argument("--list-devices", action="store_true",
                     help="Verfügbare Trainingsgeräte als JSON auf stdout "
                          "(für die GUI-Geräteauswahl).")
    args = ap.parse_args()

    if args.check:
        print("AVAILABLE" if available() else "UNAVAILABLE")
        return

    if args.list_devices:
        print(json.dumps(list_devices(), ensure_ascii=False))
        return

    if not args.dataset_dir or not args.output:
        ap.error("--dataset-dir und --output sind erforderlich, außer bei --check/--list-devices")

    train_and_export(args.dataset_dir, args.output, epochs=args.epochs,
                      device=args.device, imgsz=args.imgsz, base_model=args.base_model)


if __name__ == "__main__":
    main()
