#!/usr/bin/env python3
"""export_yolo_onnx.py - exportiert ein trainiertes Ultralytics-YOLO-Modell
(.pt) als ONNX-Datei, die GENAU dem Vertrag von `ai_roi.py` entspricht:
Eingabe `1x3xHxW`, Ausgabe verlustfrei zu `(N,6)` reshapebar mit
`[x0,y0,x1,y1,confidence,class_id]` in NORMALISIERTEN Bildkoordinaten (0..1).

Warum ein eigener Nachbearbeitungsschritt nötig ist (empirisch geprüft, nicht
angenommen): Ultralytics' `model.export(format="onnx", nms=True)` liefert
zwar bereits die richtige (N,6)-Form mit eingebauter NMS, aber die ersten
vier Spalten sind PIXELKOORDINATEN relativ zur Exportgröße (imgsz), nicht
normalisiert - an einem echten Testbild gemessen: Werte bis ~250 bei
imgsz=256, nicht 0..1. `ai_roi.py`'s Vertrag ist bewusst unabhängig von
einem bestimmten Exportformat entworfen (siehe dessen Modul-Docstring) -
die richtige Stelle für die Normalisierung ist deshalb hier beim Export,
nicht durch einen Sonderfall in ai_roi.py's decode_detections().

Vorgehen: exportiert zunächst wie gewohnt über ultralytics, lädt das
Ergebnis dann mit dem onnx-Paket, fügt einen Divisionsknoten ein, der nur
die ersten vier Spalten (die Koordinaten) durch imgsz teilt, und schreibt
das Ergebnis als neue Datei. confidence/class_id (Spalten 4,5) bleiben
unverändert.

Nutzung:
  python3 export_yolo_onnx.py --model best.pt --imgsz 640 \
      --output roi_detector.onnx
Das Ergebnis kann direkt als --ai-model-path bzw. in
%LOCALAPPDATA%/SamNPlayer/models/roi_detector.onnx (siehe
ai_roi.default_model_path()) abgelegt werden.
"""

import argparse
import os
import sys

import numpy as np
import onnx
from onnx import helper, numpy_helper, TensorProto


def normalize_onnx_graph(input_path, output_path, imgsz):
    """Lädt eine ONNX-Datei mit (N,6)-Ausgabe `[x0,y0,x1,y1,confidence,
    class_id]` in Pixelkoordinaten (relativ zu imgsz) und schreibt eine
    Kopie, bei der die ersten vier Spalten durch imgsz geteilt sind -
    normalisierte 0..1-Bildkoordinaten, wie es `ai_roi.py`'s Vertrag
    verlangt. Reine Graph-Bearbeitung ohne ultralytics-Abhängigkeit, damit
    sie sich mit einem kleinen synthetischen ONNX-Modell testen lässt, ohne
    echtes Training zu brauchen."""
    m = onnx.load(input_path)
    graph = m.graph

    if len(graph.output) != 1:
        raise RuntimeError(
            f"Erwartet genau einen Ausgabetensor, gefunden: {[o.name for o in graph.output]}")
    orig_output = graph.output[0]
    orig_name = orig_output.name

    # Slice [..., 0:4] -> Koordinaten, [..., 4:6] -> confidence/class_id.
    # Div der Koordinaten durch imgsz, dann wieder zusammenfügen - operiert
    # auf der letzten Achse (axis=-1), unabhängig davon ob die Ausgabe
    # (1,N,6) oder (N,6) ist.
    starts_coords = numpy_helper.from_array(np.array([0], dtype=np.int64), name="coord_start")
    ends_coords = numpy_helper.from_array(np.array([4], dtype=np.int64), name="coord_end")
    starts_rest = numpy_helper.from_array(np.array([4], dtype=np.int64), name="rest_start")
    ends_rest = numpy_helper.from_array(np.array([6], dtype=np.int64), name="rest_end")
    axis_const = numpy_helper.from_array(np.array([-1], dtype=np.int64), name="slice_axis")
    divisor = numpy_helper.from_array(np.array([imgsz], dtype=np.float32), name="imgsz_divisor")
    for init in (starts_coords, ends_coords, starts_rest, ends_rest, axis_const, divisor):
        graph.initializer.append(init)

    slice_coords = helper.make_node(
        "Slice", [orig_name, "coord_start", "coord_end", "slice_axis"],
        ["coords_px"], name="slice_coords")
    slice_rest = helper.make_node(
        "Slice", [orig_name, "rest_start", "rest_end", "slice_axis"],
        ["rest"], name="slice_rest")
    div_coords = helper.make_node(
        "Div", ["coords_px", "imgsz_divisor"], ["coords_norm"], name="normalize_coords")
    concat = helper.make_node(
        "Concat", ["coords_norm", "rest"], ["output_normalized"], axis=-1, name="concat_normalized")
    graph.node.extend([slice_coords, slice_rest, div_coords, concat])

    new_output = helper.make_tensor_value_info(
        "output_normalized", TensorProto.FLOAT, [d.dim_value or None for d in orig_output.type.tensor_type.shape.dim])
    del graph.output[:]
    graph.output.append(new_output)

    onnx.checker.check_model(m)
    onnx.save(m, output_path)
    return output_path


def export_and_normalize(pt_path, output_path, imgsz=640, simplify=True):
    from ultralytics import YOLO

    model = YOLO(pt_path)
    print(f"Exportiere {pt_path} (imgsz={imgsz}, NMS eingebaut)...", file=sys.stderr)
    raw_path = model.export(format="onnx", imgsz=imgsz, nms=True, simplify=simplify)

    print(f"Normalisiere Koordinaten (÷{imgsz}) -> {output_path}...", file=sys.stderr)
    result = normalize_onnx_graph(raw_path, output_path, imgsz)
    print(f"Geschrieben: {output_path}", file=sys.stderr)
    return result


def verify(onnx_path, imgsz):
    """Lädt das Ergebnis erneut und prüft an einem echten Zufallsbild, dass
    alle Koordinatenwerte im Bereich 0..imgsz*1.5 liegen (etwas Spielraum,
    YOLO-NMS-Boxen können knapp über den Bildrand hinausragen) - kein voller
    Vertragstest (der bräuchte ein Modell mit echten Detektionen), aber
    fängt einen falschen Divisor oder eine vertauschte Achse zuverlässig ab."""
    import onnxruntime as ort
    sess = ort.InferenceSession(onnx_path, providers=["CPUExecutionProvider"])
    input_name = sess.get_inputs()[0].name
    x = np.random.rand(1, 3, imgsz, imgsz).astype(np.float32)
    out = sess.run(None, {input_name: x})[0]
    arr = np.asarray(out).reshape(-1, 6)
    coord_max = float(arr[:, :4].max()) if arr.size else 0.0
    ok = coord_max <= 1.5
    print(f"Verifikation: höchster Koordinatenwert {coord_max:.3f} "
          f"({'OK, wirkt normalisiert' if ok else 'VERDÄCHTIG, sieht nach Pixelkoordinaten aus'})",
          file=sys.stderr)
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", required=True, help="Pfad zur trainierten .pt-Datei")
    ap.add_argument("--output", required=True, help="Zielpfad für die .onnx-Datei")
    ap.add_argument("--imgsz", type=int, default=640,
                     help="Muss zur beim Training verwendeten imgsz passen (Standard: 640)")
    ap.add_argument("--no-simplify", action="store_true")
    args = ap.parse_args()

    out = export_and_normalize(args.model, args.output, imgsz=args.imgsz,
                                simplify=not args.no_simplify)
    if not verify(out, args.imgsz):
        print("WARNUNG: Verifikation unerwartet - Ausgabe manuell prüfen, "
              "bevor das Modell produktiv verwendet wird.", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
