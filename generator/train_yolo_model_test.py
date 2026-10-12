"""Test für train_yolo_model.py.

Läuft OHNE echtes Training und OHNE dass ultralytics installiert sein muss
- geprüfte Teile: Fail-Fast, Geräteauflösung (auto/cuda/directml/mps/cpu),
--list-devices, CLI-Defaults. ultralytics wird wo nötig per sys.modules
simuliert (wie beim available()-Negativtest).

Ausführen: python3 generator/train_yolo_model_test.py
"""

import io
import json
import os
import sys
import tempfile
import types
from contextlib import redirect_stdout
from unittest.mock import patch

import train_yolo_model


def _fake_ultralytics(train_fn=None):
    """Minimales ultralytics-Modul, damit 'from ultralytics import YOLO' geht."""
    mod = types.ModuleType("ultralytics")

    class YOLO:
        def __init__(self, *a, **k):
            pass

        def train(self, **kwargs):
            if train_fn:
                train_fn(kwargs)

    mod.YOLO = YOLO
    return mod


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    # --- available()/--check -------------------------------------------------
    with patch.dict(sys.modules, {"ultralytics": None}):
        check("available() meldet False, wenn ultralytics fehlt (simuliert)",
              train_yolo_model.available() is False, "")

        with tempfile.TemporaryDirectory() as tmp:
            try:
                train_yolo_model.train_and_export(tmp, os.path.join(tmp, "out.onnx"))
                check("train_and_export wirft bei fehlendem ultralytics", False)
            except RuntimeError as exc:
                check("train_and_export wirft eine klare RuntimeError statt eines rohen "
                      "ModuleNotFoundError-Tracebacks", "ultralytics" in str(exc), str(exc))
                check("Fehlermeldung nennt den pip-install-Befehl",
                      "requirements-ai-train.txt" in str(exc), str(exc))

        out = io.StringIO()
        with redirect_stdout(out):
            sys.argv = ["train_yolo_model.py", "--check"]
            train_yolo_model.main()
        check("--check meldet UNAVAILABLE ohne ultralytics",
              out.getvalue().strip() == "UNAVAILABLE", repr(out.getvalue()))

    with patch.dict(sys.modules, {"ultralytics": _fake_ultralytics()}):
        check("available() meldet True mit simuliertem ultralytics",
              train_yolo_model.available() is True, "")
        out = io.StringIO()
        with redirect_stdout(out):
            sys.argv = ["train_yolo_model.py", "--check"]
            train_yolo_model.main()
        check("--check meldet AVAILABLE mit simuliertem ultralytics",
              out.getvalue().strip() == "AVAILABLE", repr(out.getvalue()))

    # --- list_devices / --list-devices ---------------------------------------
    devices = train_yolo_model.list_devices()
    ids = [d["id"] for d in devices]
    check("list_devices enthält auto/cuda/directml/mps/cpu",
          ids == ["auto", "cuda", "directml", "mps", "cpu"], str(ids))
    check("auto und cpu sind immer available",
          devices[0]["available"] is True and devices[-1]["available"] is True, str(devices))

    out = io.StringIO()
    with redirect_stdout(out):
        sys.argv = ["train_yolo_model.py", "--list-devices"]
        train_yolo_model.main()
    parsed = json.loads(out.getvalue())
    check("--list-devices liefert JSON-Liste", isinstance(parsed, list) and len(parsed) == 5,
          repr(out.getvalue()[:200]))

    # --- resolve_device ------------------------------------------------------
    with patch.object(train_yolo_model, "_cuda_available", return_value=True), \
         patch.object(train_yolo_model, "_mps_available", return_value=False), \
         patch.object(train_yolo_model, "_directml_device", return_value=None):
        check("auto → cuda wenn CUDA da", train_yolo_model.resolve_device("auto") == "0")
        check("cuda → 0", train_yolo_model.resolve_device("cuda") == "0")

    with patch.object(train_yolo_model, "_cuda_available", return_value=False), \
         patch.object(train_yolo_model, "_mps_available", return_value=False), \
         patch.object(train_yolo_model, "_directml_device", return_value="dml-fake"):
        check("auto → directml ohne CUDA/MPS",
              train_yolo_model.resolve_device("auto") == "dml-fake")
        check("directml explizit", train_yolo_model.resolve_device("directml") == "dml-fake")

    with patch.object(train_yolo_model, "_cuda_available", return_value=False), \
         patch.object(train_yolo_model, "_mps_available", return_value=False), \
         patch.object(train_yolo_model, "_directml_device", return_value=None):
        check("auto → cpu ohne GPU", train_yolo_model.resolve_device("auto") == "cpu")
        try:
            train_yolo_model.resolve_device("cuda")
            check("cuda ohne GPU wirft", False)
        except RuntimeError as exc:
            check("cuda ohne GPU nennt DirectML als Alternative",
                  "directml" in str(exc).lower(), str(exc))
        try:
            train_yolo_model.resolve_device("directml")
            check("directml ohne torch-directml wirft", False)
        except RuntimeError as exc:
            check("directml-Fehler nennt torch-directml",
                  "torch-directml" in str(exc), str(exc))

    check("cpu passthrough", train_yolo_model.resolve_device("cpu") == "cpu")
    check("numerischer Index passthrough", train_yolo_model.resolve_device("1") == "1")

    # --- train_and_export: data.yaml Fail-Fast (ultralytics simuliert) -------
    with patch.dict(sys.modules, {"ultralytics": _fake_ultralytics()}):
        with tempfile.TemporaryDirectory() as tmp:
            try:
                train_yolo_model.train_and_export(tmp, os.path.join(tmp, "out.onnx"))
                check("wirft bei fehlender data.yaml", False)
            except RuntimeError as exc:
                check("wirft RuntimeError bei fehlender data.yaml",
                      "data.yaml" in str(exc), str(exc))
                check("Fehlermeldung verweist auf Use for training / bootstrap",
                      "Use for training" in str(exc) or "bootstrap_yolo_dataset.py" in str(exc),
                      str(exc))

    # --- prepare_dataset_for_training: empty val heal + empty dataset error --
    with tempfile.TemporaryDirectory() as tmp:
        # train-only tiny dataset (the Windows portable failure mode)
        os.makedirs(os.path.join(tmp, "images", "train"))
        os.makedirs(os.path.join(tmp, "labels", "train"))
        os.makedirs(os.path.join(tmp, "images", "val"))
        os.makedirs(os.path.join(tmp, "labels", "val"))
        with open(os.path.join(tmp, "images", "train", "a.jpg"), "wb") as f:
            f.write(b"fake")
        with open(os.path.join(tmp, "labels", "train", "a.txt"), "w") as f:
            f.write("0 0.5 0.5 0.2 0.2\n")
        train_n, val_n = train_yolo_model.prepare_dataset_for_training(tmp)
        check("prepare: heals empty val from train",
              train_n >= 1 and val_n >= 1, f"train={train_n} val={val_n}")
        check("prepare: val file exists after heal",
              os.path.isfile(os.path.join(tmp, "images", "val", "a.jpg")))

    with tempfile.TemporaryDirectory() as tmp:
        os.makedirs(os.path.join(tmp, "images", "train"))
        os.makedirs(os.path.join(tmp, "images", "val"))
        try:
            train_yolo_model.prepare_dataset_for_training(tmp)
            check("prepare: empty dataset wirft", False)
        except RuntimeError as exc:
            check("prepare: empty dataset nennt labeled image / Use for training",
                  "labeled" in str(exc).lower() and "Use for training" in str(exc),
                  str(exc))

    # train_and_export fails before YOLO when dataset has yaml but no images
    with patch.dict(sys.modules, {"ultralytics": _fake_ultralytics()}):
        with tempfile.TemporaryDirectory() as tmp:
            with open(os.path.join(tmp, "data.yaml"), "w") as f:
                f.write("path: .\ntrain: images/train\nval: images/val\nnames:\n  0: x\n")
            os.makedirs(os.path.join(tmp, "images", "train"))
            os.makedirs(os.path.join(tmp, "images", "val"))
            try:
                train_yolo_model.train_and_export(tmp, os.path.join(tmp, "out.onnx"))
                check("train_and_export wirft bei leerem Datensatz", False)
            except RuntimeError as exc:
                check("train_and_export Fail-Fast bei 0 Bildern (nicht Ultralytics-Assertion)",
                      "empty" in str(exc).lower() or "labeled" in str(exc).lower(),
                      str(exc))

    # --- train_and_export: batch=-1 + resolved device ------------------------
    captured_train_kwargs = {}

    def capture_train(kwargs):
        captured_train_kwargs.update(kwargs)

    with tempfile.TemporaryDirectory() as tmp:
        with open(os.path.join(tmp, "data.yaml"), "w") as f:
            f.write("path: .\ntrain: images/train\nval: images/val\nnames:\n  0: x\n")
        # Need ≥1 train image or prepare_dataset_for_training fails before train()
        os.makedirs(os.path.join(tmp, "images", "train"))
        os.makedirs(os.path.join(tmp, "labels", "train"))
        with open(os.path.join(tmp, "images", "train", "x.jpg"), "wb") as f:
            f.write(b"fake")
        with open(os.path.join(tmp, "labels", "train", "x.txt"), "w") as f:
            f.write("0 0.5 0.5 0.1 0.1\n")
        with patch.dict(sys.modules, {"ultralytics": _fake_ultralytics(capture_train)}), \
             patch.object(train_yolo_model, "resolve_device", return_value="cpu"):
            try:
                train_yolo_model.train_and_export(tmp, os.path.join(tmp, "out.onnx"),
                                                   device="auto")
            except RuntimeError:
                pass  # erwartet - Fake schreibt keine best.pt
        check("model.train() bekommt batch=-1 (Autobatch statt fester Größe)",
              captured_train_kwargs.get("batch") == -1, str(captured_train_kwargs))
        check("model.train() bekommt aufgelöstes Gerät",
              captured_train_kwargs.get("device") == "cpu", str(captured_train_kwargs))
        check("train_and_export heilt leeres val vor model.train()",
              os.path.isfile(os.path.join(tmp, "images", "val", "x.jpg")))

    # --- main(): CLI-Argumente -----------------------------------------------
    captured = {}

    def fake_train_and_export(dataset_dir, output_path, epochs=100, device="auto",
                               imgsz=640, base_model="yolov8n.pt", project_dir=None,
                               run_name="samnplayer_roi"):
        captured.clear()
        captured.update(dataset_dir=dataset_dir, output_path=output_path, epochs=epochs,
                         device=device, imgsz=imgsz, base_model=base_model)

    orig = train_yolo_model.train_and_export
    train_yolo_model.train_and_export = fake_train_and_export
    try:
        sys.argv = ["train_yolo_model.py", "--dataset-dir", "/tmp/ds", "--output", "/tmp/out.onnx"]
        train_yolo_model.main()
        check("Pfade kommen unverändert an",
              captured.get("dataset_dir") == "/tmp/ds" and captured.get("output_path") == "/tmp/out.onnx",
              str(captured))
        check("Standard-Epochen 100", captured.get("epochs") == 100, str(captured))
        check("Standard-Gerät auto (CUDA→MPS→DirectML→CPU)",
              captured.get("device") == "auto", str(captured))
        check("Standard-Bildgröße 640", captured.get("imgsz") == 640, str(captured))
        check("Standard-Basismodell yolov8n.pt (kleinstes/schnellstes)",
              captured.get("base_model") == "yolov8n.pt", str(captured))

        sys.argv = ["train_yolo_model.py", "--dataset-dir", "/tmp/ds", "--output", "/tmp/out.onnx",
                    "--epochs", "5", "--device", "directml", "--imgsz", "320",
                    "--base-model", "yolov8s.pt"]
        train_yolo_model.main()
        check("--epochs überschreibt den Standardwert", captured.get("epochs") == 5, str(captured))
        check("--device überschreibt den Standardwert",
              captured.get("device") == "directml", str(captured))
        check("--imgsz überschreibt den Standardwert", captured.get("imgsz") == 320, str(captured))
        check("--base-model überschreibt den Standardwert",
              captured.get("base_model") == "yolov8s.pt", str(captured))
    finally:
        train_yolo_model.train_and_export = orig

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
