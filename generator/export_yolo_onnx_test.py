"""Test für export_yolo_onnx.py - die Koordinaten-Normalisierung nach dem
ultralytics-ONNX-Export.

Baut dafür ein winziges SYNTHETISCHES ONNX-Modell von Hand (ein Identity-Knoten
auf einer festen (1,3,6)-Konstante), statt echtes Training zu brauchen - es
geht nur darum, dass normalize_onnx_graph() die ersten vier Spalten korrekt
durch imgsz teilt und Spalte 4/5 (confidence, class_id) unverändert lässt.
Läuft NICHT ohne die `onnx`- und `onnxruntime`-Pakete (siehe
generator/requirements-ai-train.txt) - beides optionale Trainingswerkzeuge,
kein Bestandteil der normalen Laufzeit, deshalb nicht in requirements-ai.txt
und nicht Teil der schnellen CI-Testsuite.

Ausführen: python3 generator/export_yolo_onnx_test.py
"""

import sys
import tempfile
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
from onnx import helper, numpy_helper, TensorProto

import export_yolo_onnx as e

IMGSZ = 256
# [x0,y0,x1,y1,confidence,class_id] in Pixelkoordinaten relativ zu IMGSZ -
# wie es ultralytics' eigener nms=True-Export tatsächlich liefert (empirisch
# geprüft, siehe Moduldoc von export_yolo_onnx.py).
RAW_ROW = [10.0, 20.0, 110.0, 220.0, 0.87, 2.0]


def make_fake_raw_export(path):
    """Ein Modell mit Eingabe 1x3xIMGSZxIMGSZ (ungenutzt) und einer festen
    (1,1,6)-Ausgabe-Konstante - genug, um normalize_onnx_graph() an einer
    echten (wenn auch trivialen) ONNX-Datei zu prüfen."""
    inp = helper.make_tensor_value_info("images", TensorProto.FLOAT, [1, 3, IMGSZ, IMGSZ])
    out = helper.make_tensor_value_info("output0", TensorProto.FLOAT, [1, 1, 6])
    const_val = numpy_helper.from_array(np.array([[RAW_ROW]], dtype=np.float32), name="const_val")
    const_node = helper.make_node("Constant", [], ["output0"], value=const_val, name="const_node")
    graph = helper.make_graph([const_node], "fake_yolo_export", [inp], [out])
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 18)])
    onnx.checker.check_model(model)
    onnx.save(model, path)


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory() as tmp:
        raw_path = str(Path(tmp) / "raw.onnx")
        norm_path = str(Path(tmp) / "normalized.onnx")
        make_fake_raw_export(raw_path)

        e.normalize_onnx_graph(raw_path, norm_path, imgsz=IMGSZ)

        sess = ort.InferenceSession(norm_path, providers=["CPUExecutionProvider"])
        x = np.zeros((1, 3, IMGSZ, IMGSZ), dtype=np.float32)
        out = sess.run(None, {"images": x})[0]
        row = np.asarray(out).reshape(-1, 6)[0]

        expected_coords = [v / IMGSZ for v in RAW_ROW[:4]]
        check("Koordinaten (Spalten 0-3) sind durch imgsz geteilt",
              np.allclose(row[:4], expected_coords), f"{row[:4]} != {expected_coords}")
        check("confidence (Spalte 4) bleibt unverändert",
              row[4] == RAW_ROW[4], str(row[4]))
        check("class_id (Spalte 5) bleibt unverändert",
              row[5] == RAW_ROW[5], str(row[5]))

        # verify() soll an genau diesem Ergebnis "normalisiert" melden, nicht
        # "sieht nach Pixelkoordinaten aus".
        check("verify() bestätigt normalisierte Koordinaten",
              e.verify(norm_path, IMGSZ) is True, "")

    # --- verify() erkennt auch die Gegenrichtung: unnormalisierte Datei -----
    with tempfile.TemporaryDirectory() as tmp2:
        raw_path2 = str(Path(tmp2) / "raw.onnx")
        make_fake_raw_export(raw_path2)
        check("verify() an einer UNnormalisierten Datei meldet false (Pixelkoordinaten erkannt)",
              e.verify(raw_path2, IMGSZ) is False, "")

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
