"""Test für ai_roi.py - den optionalen ONNX-ROI-Vorschlag.

Läuft komplett OHNE onnxruntime und OHNE ein echtes Modell: decode/select
sind reine Funktionen, und find_roi() nimmt über _run_model_fn eine
Attrappe entgegen (derselbe Injektionstrick, den track_by_scenes() für
roi_finder selbst schon nutzt). Damit ist die Nachverarbeitung geprüft,
ohne Modellgewichte ins Repository legen oder herunterladen zu müssen.

Ausführen: python3 generator/ai_roi_test.py
"""

import subprocess
import sys
import tempfile
from pathlib import Path

import cv2
import numpy as np

import ai_roi

W, H, FPS, FRAMES = 320, 240, 25, 50
# Objekt bei 70% Bildbreite, 40% Bildhöhe - so lässt sich prüfen, dass die
# normalisierten Modellkoordinaten korrekt in Pixel des ECHTEN Frames
# umgerechnet werden, nicht nur der quadratischen Modelleingabe.
OBJECT_NORM = (0.60, 0.30, 0.80, 0.50)  # x0,y0,x1,y1


def write_video(path):
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    rng = np.random.default_rng(1)
    bg = np.full((H, W, 3), 40, np.uint8)
    for _ in range(60):
        x, y = int(rng.integers(0, W)), int(rng.integers(0, H))
        cv2.rectangle(bg, (x, y), (x + 12, y + 12), (90, 90, 90), -1)
    for _ in range(FRAMES):
        vw.write(bg)
    vw.release()


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    # --- decode_detections: Schwelle und entartete Boxen --------------------
    raw = np.array([
        [0.1, 0.1, 0.2, 0.2, 0.90, 0],   # klar über der Schwelle
        [0.3, 0.3, 0.4, 0.4, 0.10, 0],   # unter der Schwelle -> raus
        [0.5, 0.5, 0.4, 0.4, 0.99, 0],   # x1<x0 und y1<y0 -> entartet, raus
    ])
    detections = ai_roi.decode_detections(raw, confidence_threshold=0.35)
    check("Schwelle filtert niedrige Konfidenz", len(detections) == 1, str(detections))
    check("entartete Box wird verworfen",
          all(d["confidence"] != 0.99 for d in detections), str(detections))

    # --- decode_detections: Reshape akzeptiert flache Eingabe ---------------
    flat = ai_roi.decode_detections([0.1, 0.1, 0.2, 0.2, 0.9, 0], confidence_threshold=0.5)
    check("akzeptiert eine einzelne flache Box (reshape (-1,6))", len(flat) == 1, str(flat))

    # --- select_best_box: höchste Konfidenz gewinnt, korrekte Pixelumrechnung
    dets = [
        {"x0": 0.1, "y0": 0.1, "x1": 0.2, "y1": 0.2, "confidence": 0.5, "class_id": 0},
        {"x0": 0.60, "y0": 0.30, "x1": 0.80, "y1": 0.50, "confidence": 0.9, "class_id": 0},
    ]
    box = ai_roi.select_best_box(dets, frame_w=1000, frame_h=1000)
    check("wählt die Box mit höherer Konfidenz", box == (600, 300, 200, 200), str(box))

    # --- select_best_box: Mindestgröße und Bildgrenzen -----------------------
    tiny = [{"x0": 0.50, "y0": 0.50, "x1": 0.501, "y1": 0.501, "confidence": 0.9, "class_id": 0}]
    box_tiny = ai_roi.select_best_box(tiny, frame_w=100, frame_h=100, min_size_px=16)
    check("erzwingt Mindestgröße", box_tiny[2] >= 16 and box_tiny[3] >= 16, str(box_tiny))

    edge = [{"x0": 0.95, "y0": 0.95, "x1": 1.0, "y1": 1.0, "confidence": 0.9, "class_id": 0}]
    box_edge = ai_roi.select_best_box(edge, frame_w=100, frame_h=100, min_size_px=16)
    check("bleibt am Bildrand innerhalb der Grenzen",
          box_edge[0] + box_edge[2] <= 100 and box_edge[1] + box_edge[3] <= 100, str(box_edge))

    check("keine Detektion -> None statt Absturz",
          ai_roi.select_best_box([], 100, 100) is None, "")

    # --- select_two_best_boxes: zwei getrennte Objekte ----------------------
    two_dets = [
        {"x0": 0.05, "y0": 0.05, "x1": 0.15, "y1": 0.15, "confidence": 0.9, "class_id": 0},
        {"x0": 0.70, "y0": 0.70, "x1": 0.80, "y1": 0.80, "confidence": 0.7, "class_id": 0},
    ]
    b1, b2 = ai_roi.select_two_best_boxes(two_dets, frame_w=1000, frame_h=1000)
    check("box1 ist die höchste Konfidenz", b1 == (50, 50, 100, 100), str((b1, b2)))
    check("box2 ist das zweite, getrennte Objekt", b2 == (700, 700, 100, 100), str((b1, b2)))

    # --- select_two_best_boxes: überlappende zweite Detektion wird verworfen
    overlapping = [
        {"x0": 0.10, "y0": 0.10, "x1": 0.30, "y1": 0.30, "confidence": 0.9, "class_id": 0},
        {"x0": 0.12, "y0": 0.12, "x1": 0.32, "y1": 0.32, "confidence": 0.6, "class_id": 0},
    ]
    b1o, b2o = ai_roi.select_two_best_boxes(overlapping, frame_w=1000, frame_h=1000)
    check("stark überlappende zweite Detektion (dasselbe Objekt) wird NICHT als box2 übernommen",
          b2o is None, str((b1o, b2o)))

    # --- select_two_best_boxes: nur eine Detektion -> box2 ist None ---------
    b1s, b2s = ai_roi.select_two_best_boxes(two_dets[:1], frame_w=1000, frame_h=1000)
    check("eine einzelne Detektion liefert box2=None statt einer erfundenen zweiten Box",
          b1s is not None and b2s is None, str((b1s, b2s)))

    check("keine Detektion -> (None, None) statt Absturz",
          ai_roi.select_two_best_boxes([], 100, 100) == (None, None), "")

    # --- class-aware: preferred_class_ids filters when matches exist --------
    mixed = [
        {"x0": 0.10, "y0": 0.10, "x1": 0.20, "y1": 0.20, "confidence": 0.99, "class_id": 0},
        {"x0": 0.60, "y0": 0.30, "x1": 0.80, "y1": 0.50, "confidence": 0.70, "class_id": 1},
    ]
    box_pref = ai_roi.select_best_box(mixed, frame_w=1000, frame_h=1000,
                                       preferred_class_ids=[1])
    check("preferred_class_ids wählt Klasse 1 trotz niedrigerer Konfidenz",
          box_pref == (600, 300, 200, 200), str(box_pref))

    box_miss = ai_roi.select_best_box(mixed, frame_w=1000, frame_h=1000,
                                      preferred_class_ids=[9])
    check("preferred ohne Treffer fail-closed (kein Klassen-Fallback)",
          box_miss is None, str(box_miss))

    # --- depth rank (opt-in): soft bonus from support_signals ----------------
    gray = np.zeros((200, 200), dtype=np.uint8)
    gray[:, :100] = 30
    gray[:, 100:] = 220
    rng = np.random.default_rng(1)
    gray[:, :100] = np.clip(
        gray[:, :100].astype(np.int16) + rng.integers(0, 40, size=(200, 100)), 0, 255
    ).astype(np.uint8)
    import support_signals
    depth_map = support_signals.relative_depth_map(gray)
    # Manual contrast map so the large left ROI clearly beats a slightly higher-conf flat right ROI.
    depth_map = np.zeros((200, 200), dtype=np.float32)
    depth_map[:, :100] = 0.95
    depth_map[:, 100:] = 0.05
    close_dets = [
        {"x0": 0.55, "y0": 0.10, "x1": 0.85, "y1": 0.25, "confidence": 0.92, "class_id": 0},
        {"x0": 0.05, "y0": 0.05, "x1": 0.45, "y1": 0.95, "confidence": 0.90, "class_id": 0},
    ]
    box_no_depth = ai_roi.select_best_box(close_dets, 200, 200, use_depth_rank=False)
    box_depth = ai_roi.select_best_box(
        close_dets, 200, 200, use_depth_rank=True, depth_map=depth_map)
    check("use_depth_rank=False keeps confidence-only winner",
          box_no_depth == (110, 20, 60, 30), str(box_no_depth))
    check("use_depth_rank=True can prefer higher depth-contrast ROI",
          box_depth == (10, 10, 80, 180), str(box_depth))

    two_class = [
        {"x0": 0.05, "y0": 0.05, "x1": 0.15, "y1": 0.15, "confidence": 0.95, "class_id": 0},
        {"x0": 0.08, "y0": 0.40, "x1": 0.18, "y1": 0.50, "confidence": 0.90, "class_id": 0},
        {"x0": 0.70, "y0": 0.70, "x1": 0.80, "y1": 0.80, "confidence": 0.60, "class_id": 1},
    ]
    b1c, b2c = ai_roi.select_two_best_boxes(
        two_class, frame_w=1000, frame_h=1000, preferred_class_ids=[0, 1])
    check("zwei bevorzugte Klassen: box2 nimmt die andere Klasse, nicht denselben Typ",
          b2c == (700, 700, 100, 100), str((b1c, b2c)))

    ids = ai_roi.resolve_preferred_class_ids(
        "hand,breast", {"hand": 0, "Breast": 1, "penis": 2})
    check("resolve_preferred_class_ids mappt Namen case-insensitive",
          ids == [0, 1], str(ids))
    check("resolve_preferred_class_ids akzeptiert numerische IDs",
          ai_roi.resolve_preferred_class_ids("0,2") == [0, 2], "")

    # --- strict semantic target: never fall back to the high-motion/wrong class
    strict_det, strict_box = ai_roi.select_strict_detection(
        mixed, expected_class_id=1, frame_w=1000, frame_h=1000)
    check("strict target wählt Glans-Klasse trotz stärkerem Oberschenkel-Distraktor",
          strict_det["class_id"] == 1 and strict_box == (600, 300, 200, 200),
          str((strict_det, strict_box)))

    strict_code = ""
    try:
        ai_roi.select_strict_detection(mixed[:1], expected_class_id=1,
                                       frame_w=1000, frame_h=1000)
    except ai_roi.StrictClassBindingError as exc:
        strict_code = exc.code
    check("strict target fällt ohne Zielklasse niemals auf fremde Klasse zurück",
          strict_code == "target_not_detected", strict_code)

    weak_target = [{
        "x0": 0.6, "y0": 0.3, "x1": 0.8, "y1": 0.5,
        "confidence": 0.20, "class_id": 1,
    }]
    strict_code = ""
    try:
        ai_roi.select_strict_detection(weak_target, expected_class_id=1,
                                       frame_w=1000, frame_h=1000,
                                       confidence_threshold=0.35)
    except ai_roi.StrictClassBindingError as exc:
        strict_code = exc.code
    check("strict target meldet Ziel unter Konfidenzschwelle ehrlich",
          strict_code == "below_confidence", strict_code)

    ambiguous = [
        {"x0": 0.1, "y0": 0.1, "x1": 0.2, "y1": 0.2,
         "confidence": 0.90, "class_id": 1},
        {"x0": 0.7, "y0": 0.7, "x1": 0.8, "y1": 0.8,
         "confidence": 0.86, "class_id": 1},
    ]
    strict_code = ""
    try:
        ai_roi.select_strict_detection(ambiguous, expected_class_id=1,
                                       frame_w=1000, frame_h=1000,
                                       ambiguity_margin=0.08)
    except ai_roi.StrictClassBindingError as exc:
        strict_code = exc.code
    check("zwei gleich plausible Zielboxen werden nicht geraten",
          strict_code == "ambiguous_target", strict_code)

    check("kanonischer Manifestname löst strikt auf",
          ai_roi.resolve_expected_class_id("glans", {"glans": 3}) == 3)
    check("deutscher Manifestalias löst auf dieselbe kanonische Klasse auf",
          ai_roi.resolve_expected_class_id("glans", {"eichel": 3}) == 3)
    strict_code = ""
    try:
        ai_roi.resolve_expected_class_id("glans", {"glans": 3, "eichel": 4})
    except ai_roi.StrictClassBindingError as exc:
        strict_code = exc.code
    check("widersprüchliche Manifestaliases werden abgelehnt",
          strict_code == "class_conflict", strict_code)
    strict_code = ""
    try:
        ai_roi.resolve_expected_class_id("glans", {"glans": 3, "penis": 3})
    except ai_roi.StrictClassBindingError as exc:
        strict_code = exc.code
    check("dieselbe Modell-ID darf nicht zwei Körperklassen bedeuten",
          strict_code == "class_conflict", strict_code)

    # --- available(): ehrliche Antwort ohne onnxruntime/Modell ---------------
    check("available() meldet False ohne Modell/Laufzeit unter erfundenem Pfad",
          ai_roi.available(model_path="/nicht/vorhanden.onnx") is False, "")

    # --- find_roi() Ende-zu-Ende mit injizierter Modell-Attrappe --------------
    with tempfile.TemporaryDirectory() as tmp:
        video = Path(tmp) / "object.mp4"
        write_video(video)

        invalid_manifest = Path(tmp) / "classes.json"
        invalid_manifest.write_text("{not json", encoding="utf-8")
        cli = subprocess.run(
            [sys.executable, str(Path(ai_roi.__file__)), "--video", str(video),
             "--model", str(Path(tmp) / "unused.onnx"),
             "--classes-json", str(invalid_manifest),
             "--expected-class", "glans", "--strict-class"],
            capture_output=True, text=True, check=False,
        )
        check("strict CLI meldet beschädigtes Klassenmanifest maschinenlesbar",
              cli.returncode == 2 and '"code": "manifest_invalid"' in cli.stdout,
              f"rc={cli.returncode} stdout={cli.stdout!r} stderr={cli.stderr!r}")

        def fake_model(frame_bgr):
            x0, y0, x1, y1 = OBJECT_NORM
            return np.array([[x0, y0, x1, y1, 0.95, 0.0]])

        x, y, w, h = ai_roi.find_roi(str(video), report_progress=False,
                                      _run_model_fn=fake_model)
        expected = ai_roi.select_best_box(
            ai_roi.decode_detections([[*OBJECT_NORM, 0.95, 0.0]]), W, H)
        check("find_roi liefert die vom Modell vorgeschlagene Box in Pixelkoordinaten",
              (x, y, w, h) == expected, f"{(x, y, w, h)} != {expected}")

        def no_detection_model(frame_bgr):
            return np.zeros((0, 6))

        raised = False
        try:
            ai_roi.find_roi(str(video), report_progress=False,
                             _run_model_fn=no_detection_model)
        except RuntimeError:
            raised = True
        check("find_roi wirft einen klaren Fehler ohne Detektion (kein stiller Fallback)",
              raised, "")

        # start_frame/end_frame: derselbe Vertrag wie auto_roi.find_roi, damit
        # track_by_scenes() beide Finder austauschbar per Szene aufrufen kann.
        x2, y2, w2, h2 = ai_roi.find_roi(str(video), start_frame=5, end_frame=30,
                                          report_progress=False, _run_model_fn=fake_model)
        check("find_roi nimmt start_frame/end_frame wie auto_roi.find_roi entgegen",
              (x2, y2, w2, h2) == expected, f"{(x2, y2, w2, h2)} != {expected}")

        def strict_mixed_model(frame_bgr):
            return np.array([
                [0.05, 0.55, 0.45, 0.95, 0.99, 0.0],  # wrong class / thigh
                [*OBJECT_NORM, 0.70, 1.0],              # expected semantic target
            ])

        strict_result = ai_roi.find_expected_roi(
            str(video), expected_class="glans", expected_class_id=1,
            report_progress=False, _run_model_fn=strict_mixed_model)
        check("find_expected_roi keeps the expected class despite a stronger distractor",
              strict_result["match"] is True
              and strict_result["matchedClass"] == "glans"
              and (strict_result["x"], strict_result["y"],
                   strict_result["w"], strict_result["h"]) == expected,
              str(strict_result))

        exact_calls = []

        def exact_frame_model(frame_bgr):
            exact_calls.append(frame_bgr)
            return strict_mixed_model(frame_bgr)

        exact_result = ai_roi.find_expected_roi(
            str(video), expected_class="glans", expected_class_id=1,
            report_progress=False, sample_frames=5, time_sec=1.2,
            _run_model_fn=exact_frame_model)
        check("strict product path evaluates exactly the visible preview frame",
              len(exact_calls) == 1 and exact_result["sampleIndex"] == 30,
              f"calls={len(exact_calls)} sample={exact_result['sampleIndex']}")

        preview_image = Path(tmp) / "strict-preview.png"
        cv2.imwrite(str(preview_image), np.full((H, W, 3), 60, np.uint8))
        image_calls = []

        def exact_image_model(frame_bgr):
            image_calls.append(frame_bgr.shape[:2])
            return strict_mixed_model(frame_bgr)

        image_result = ai_roi.find_expected_image(
            str(preview_image), expected_class="glans", expected_class_id=1,
            _run_model_fn=exact_image_model)
        check("strict image path evaluates exactly one supplied preview image",
              image_calls == [(H, W)] and image_result["sampleIndex"] == -1,
              f"calls={image_calls} result={image_result}")

        strict_code = ""
        try:
            ai_roi.find_expected_roi(
                str(video), expected_class="glans", expected_class_id=1,
                report_progress=False, _run_model_fn=fake_model)
        except ai_roi.StrictClassBindingError as exc:
            strict_code = exc.code
        check("find_expected_roi never accepts only the wrong detected class",
              strict_code == "target_not_detected", strict_code)

        # --- find_two_rois() Ende-zu-Ende: zwei getrennte Objekte -------------
        SECOND_OBJECT_NORM = (0.05, 0.05, 0.15, 0.15)

        def two_object_model(frame_bgr):
            x0, y0, x1, y1 = OBJECT_NORM
            sx0, sy0, sx1, sy1 = SECOND_OBJECT_NORM
            return np.array([
                [x0, y0, x1, y1, 0.95, 0.0],
                [sx0, sy0, sx1, sy1, 0.80, 0.0],
            ])

        roi1, roi2 = ai_roi.find_two_rois(str(video), report_progress=False,
                                           _run_model_fn=two_object_model)
        expected2 = ai_roi.select_best_box(
            ai_roi.decode_detections([[*SECOND_OBJECT_NORM, 0.80, 0.0]]), W, H)
        check("find_two_rois liefert roi1 wie find_roi", roi1 == expected, f"{roi1} != {expected}")
        check("find_two_rois liefert das zweite, getrennte Objekt als roi2",
              roi2 == expected2, f"{roi2} != {expected2}")

        # --- find_two_rois() mit nur einem Objekt: roi2 ist None ehrlich ------
        roi1_only, roi2_only = ai_roi.find_two_rois(str(video), report_progress=False,
                                                      _run_model_fn=fake_model)
        check("find_two_rois liefert roi2=None statt einer erfundenen Box, "
              "wenn nur ein Objekt erkannt wird", roi2_only is None, str(roi2_only))

        raised_two = False
        try:
            ai_roi.find_two_rois(str(video), report_progress=False,
                                  _run_model_fn=no_detection_model)
        except RuntimeError:
            raised_two = True
        check("find_two_rois wirft einen klaren Fehler ohne jede Detektion",
              raised_two, "")

    # --- ohne echtes Modell: klarer Fehler statt Absturz ----------------------
    raised = False
    try:
        ai_roi.find_roi("irrelevant.mp4", model_path="/nicht/vorhanden.onnx")
    except ai_roi.ModelUnavailable:
        raised = True
    check("find_roi ohne Modell wirft ModelUnavailable statt zu crashen", raised, "")

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
