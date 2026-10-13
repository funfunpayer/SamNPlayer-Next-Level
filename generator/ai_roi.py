#!/usr/bin/env python3
"""ai_roi.py - schlägt die Bewegungsregion per lokalem ONNX-Objekterkenner
vor, als KI-Alternative zu `auto_roi.py` (Rhythmus-Heuristik ohne Modell).

Warum ein eigenes Modul statt eines Umbaus von auto_roi.py: beide erfüllen
denselben Vertrag (`find_roi(video, start_frame, end_frame) -> (x,y,w,h)`)
und lassen sich darum 1:1 austauschen - `track_by_scenes()` in
generate_funscript.py nimmt jeden `roi_finder` mit dieser Signatur, siehe
den Parameter dort. auto_roi.py bleibt unverändert die Vorgabe; dieses Modul
ist eine Ergänzung, keine Ablösung.

WICHTIG - was hier NICHT passiert:
- Kein Modell liegt im Repository oder wird automatisch heruntergeladen.
  `model_path` zeigt auf eine vom Nutzer bereitgestellte .onnx-Datei; ohne
  sie meldet `available()` False und `find_roi()` einen klaren Fehler, den
  `track_by_scenes()` abfängt und die Szene überspringt (kein Tip-Reuse).
- Kein Code oder Gewicht aus FunGen 1 (PolyForm Strict) oder FunGen 2
  (geschlossenes Binary) wurde gelesen oder übernommen. Nur öffentlich
  beschriebenes VERHALTEN (fungen.app, Changelog: YOLO-Erkennung +
  Tracking) diente als Vorbild, siehe docs/TEAM_STAND.md. Der Vertrag unten
  ist eine eigene, unabhängig entworfene Schnittstelle.
- `onnxruntime` ist NICHT in requirements.txt - es bleibt eine optionale
  Abhängigkeit (siehe requirements-ai.txt), damit der Generator ohne lokale
  KI weiterhin ohne zusätzliche Installation läuft ("was nicht gebraucht
  wird, kommt nicht rein", HANDOFF.md).

MODELL-VERTRAG
---------------
Erwartet wird ein ONNX-Modell mit einem Eingabetensor `1x3xHxW` (float32,
RGB, Werte 0..1) und einem Ausgabetensor, der sich verlustfrei zu
`(N, 6)` reshapen lässt: pro Zeile `[x0, y0, x1, y1, confidence, class_id]`
in NORMALISIERTEN Bildkoordinaten (0..1, nicht Pixel - das entkoppelt die
Nachverarbeitung von der Eingabegröße). Das ist ein bewusst einfacher,
eigener Vertrag - kein Nachbau eines bestimmten YOLO-Exportformats -, damit
sich ein beliebiges kompatibel exportiertes Detektionsmodell einstecken
lässt. `decode_detections()` und `select_best_box()` sind reine Funktionen
und darum ohne echtes Modell testbar (siehe ai_roi_test.py).

Nutzung (eigenständig oder aus generate_funscript.py importiert):
  python3 ai_roi.py --video input.mp4 --model roi_detector.onnx
    -> gibt "ROI x,y,w,h" wie auto_roi.py auf stdout aus
"""

import argparse
import json
import os
import sys

import cv2
import numpy as np

import bodyparts


class ModelUnavailable(RuntimeError):
    """onnxruntime fehlt oder das Modell wurde nicht gefunden."""


class StrictClassBindingError(RuntimeError):
    """A strict semantic target could not be bound safely.

    ``code`` is stable for the Go/Wails bridge; the human message may evolve.
    Strict failures never fall back to another class or to motion auto-ROI.
    """

    def __init__(self, code, message, expected_class=""):
        super().__init__(message)
        self.code = str(code)
        self.expected_class = bodyparts.normalize(expected_class)


def default_model_path():
    """Ablageort analog zu backends.default_plugin_dir() - ein
    Nutzerkonfigurationsordner, kein Pfad im Repository/Binary."""
    local = os.environ.get("LOCALAPPDATA")
    if local:
        return os.path.join(local, "SamNPlayer", "models", "roi_detector.onnx")
    xdg = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
    return os.path.join(xdg, "SamNPlayer", "models", "roi_detector.onnx")


def available(model_path=None):
    """True, wenn onnxruntime installiert ist UND ein Modell an model_path
    (oder dem Standardpfad) liegt. Reine Prüffunktion, löst nichts aus -
    die GUI kann sie nutzen, um den KI-Button zu aktivieren/auszublenden."""
    try:
        import onnxruntime  # noqa: F401
    except ImportError:
        return False
    path = model_path or default_model_path()
    return os.path.isfile(path)


def decode_detections(raw_output, confidence_threshold=0.35):
    """Reine Funktion: rohe (N,6)-Modellausgabe -> gefilterte Detektionen.

    Kein onnxruntime nötig, darum ohne Modell testbar. raw_output ist
    alles, was sich zu (N,6) reshapen lässt (Liste, ndarray, ...).
    """
    arr = np.asarray(raw_output, dtype=float).reshape(-1, 6)
    if arr.size == 0:
        return []
    keep = arr[:, 4] >= confidence_threshold
    detections = []
    for x0, y0, x1, y1, conf, cls in arr[keep]:
        if x1 <= x0 or y1 <= y0:
            continue  # entartete Box, z.B. aus Rauschen im Rohtensor
        detections.append({
            "x0": float(x0), "y0": float(y0), "x1": float(x1), "y1": float(y1),
            "confidence": float(conf), "class_id": int(cls),
        })
    return detections


def _filter_preferred(detections, preferred_class_ids=None):
    """If preferred_class_ids is set, keep only matching detections.

    Empty preferred match → empty list (fail closed). No silent fallback to
    any other class — use find_expected_roi / strict binding when you need a
    named body class, or leave preferred unset for confidence-only ranking.
    """
    if not detections or not preferred_class_ids:
        return detections
    preferred = {int(c) for c in preferred_class_ids}
    return [d for d in detections if int(d.get("class_id", -1)) in preferred]


def _depth_rank_enabled(use_depth_rank=False):
    if use_depth_rank:
        return True
    return os.environ.get("SAMNPLAYER_DEPTH_RANK", "").strip().lower() in (
        "1", "true", "yes", "on")


def _optional_depth_map(frame_bgr, use_depth_rank=False):
    if frame_bgr is None or not _depth_rank_enabled(use_depth_rank):
        return None
    try:
        import support_signals
        gray = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2GRAY)
        return support_signals.relative_depth_map(gray)
    except Exception:
        return None


def _detection_rank_score(det, frame_w, frame_h, depth_map=None, use_depth_rank=False):
    """Confidence plus optional soft depth contrast bonus (opt-in only)."""
    score = float(det["confidence"])
    if not _depth_rank_enabled(use_depth_rank) or depth_map is None:
        return score
    try:
        import support_signals
    except ImportError:
        return score
    x0 = int(det["x0"] * frame_w)
    y0 = int(det["y0"] * frame_h)
    x1 = int(det["x1"] * frame_w)
    y1 = int(det["y1"] * frame_h)
    w = max(1, x1 - x0)
    h = max(1, y1 - y0)
    bonus = support_signals.depth_roi_confidence((x0, y0, w, h), depth_map)
    return score + 0.12 * bonus


def select_best_box(detections, frame_w, frame_h, min_size_px=16,
                    preferred_class_ids=None, use_depth_rank=False, depth_map=None):
    """Pure function: detection list (normalized coords) ->
    (x,y,w,h) in pixels for the highest-confidence box, or None.

    When preferred_class_ids is given and any detection matches, only those
    classes compete; otherwise all detections are considered (so a
    breast/hand-trained model can prefer the contact class without
    failing when that class is absent in the frame).

    When use_depth_rank is True or SAMNPLAYER_DEPTH_RANK=1, detections get a
    small bonus from support_signals.depth_roi_confidence (does not run when
    depth_map is None).
    """
    candidates = _filter_preferred(detections, preferred_class_ids)
    if not candidates:
        return None
    best = max(
        candidates,
        key=lambda d: _detection_rank_score(d, frame_w, frame_h, depth_map, use_depth_rank),
    )
    x0 = int(best["x0"] * frame_w)
    y0 = int(best["y0"] * frame_h)
    x1 = int(best["x1"] * frame_w)
    y1 = int(best["y1"] * frame_h)
    x0, x1 = sorted((max(0, x0), min(frame_w, x1)))
    y0, y1 = sorted((max(0, y0), min(frame_h, y1)))
    w = max(min_size_px, x1 - x0)
    h = max(min_size_px, y1 - y0)
    x0 = min(x0, max(0, frame_w - w))
    y0 = min(y0, max(0, frame_h - h))
    return (x0, y0, w, h)


def _iou(a, b):
    """Intersection over Union zweier Boxen in normalisierten (x0,y0,x1,y1)-
    Detektionen - reine Geometrie, kein Bezug zu Pixelkoordinaten nötig."""
    ix0, iy0 = max(a["x0"], b["x0"]), max(a["y0"], b["y0"])
    ix1, iy1 = min(a["x1"], b["x1"]), min(a["y1"], b["y1"])
    inter = max(0.0, ix1 - ix0) * max(0.0, iy1 - iy0)
    area_a = max(0.0, a["x1"] - a["x0"]) * max(0.0, a["y1"] - a["y0"])
    area_b = max(0.0, b["x1"] - b["x0"]) * max(0.0, b["y1"] - b["y0"])
    union = area_a + area_b - inter
    return inter / union if union > 0 else 0.0


def select_two_best_boxes(detections, frame_w, frame_h, min_size_px=16, max_iou=0.3,
                          preferred_class_ids=None, use_depth_rank=False, depth_map=None):
    """Like select_best_box, but picks up to TWO spatially separated objects
    for profiles that need two regions (tf/tj).

    When preferred_class_ids has more than one id, the second box prefers a
    *different* class from the first (e.g. hand + breast) before falling
    back to any low-IoU candidate. Never invents a second box: box2 is None
    when nothing sufficiently separated exists.
    """
    candidates = _filter_preferred(detections, preferred_class_ids)
    if not candidates:
        return None, None
    ranked = sorted(
        candidates,
        key=lambda d: _detection_rank_score(d, frame_w, frame_h, depth_map, use_depth_rank),
        reverse=True,
    )
    best = ranked[0]
    box1 = select_best_box([best], frame_w, frame_h, min_size_px)
    best_cls = int(best.get("class_id", -1))
    # Prefer a different class when multi-class preference is active.
    second = None
    if preferred_class_ids and len(set(int(c) for c in preferred_class_ids)) > 1:
        second = next(
            (d for d in ranked[1:]
             if int(d.get("class_id", -1)) != best_cls and _iou(d, best) <= max_iou),
            None)
    if second is None:
        second = next((d for d in ranked[1:] if _iou(d, best) <= max_iou), None)
    box2 = select_best_box([second], frame_w, frame_h, min_size_px) if second else None
    return box1, box2


def load_class_registry(path):
    """Load name->id map from classes.json (dataset dir or file path)."""
    if not path:
        return {}
    file_path = path
    if os.path.isdir(path):
        file_path = os.path.join(path, "classes.json")
    if not os.path.isfile(file_path):
        return {}
    with open(file_path, encoding="utf-8") as f:
        data = json.load(f)
    return {str(k): int(v) for k, v in data.items()} if isinstance(data, dict) else {}


def resolve_preferred_class_ids(names_or_ids, registry=None):
    """Parse 'hand,breast' / '0,1' / mixed into a list of int class ids.
    Names need registry (classes.json). Unknown names are skipped."""
    if not names_or_ids:
        return None
    if isinstance(names_or_ids, (list, tuple)):
        parts = [str(p).strip() for p in names_or_ids if str(p).strip()]
    else:
        parts = [p.strip() for p in str(names_or_ids).split(",") if p.strip()]
    if not parts:
        return None
    registry = registry or {}
    # case-insensitive name lookup
    by_lower = {str(k).lower(): int(v) for k, v in registry.items()}
    out = []
    for p in parts:
        if p.isdigit() or (p.startswith("-") and p[1:].isdigit()):
            out.append(int(p))
            continue
        cid = by_lower.get(p.lower())
        if cid is not None:
            out.append(cid)
    return out or None


def invert_class_registry(registry):
    """id -> preferred canonical name (first name that maps to that id)."""
    out = {}
    for name, cid in (registry or {}).items():
        i = int(cid)
        if i not in out:
            out[i] = bodyparts.normalize(name) or str(name)
    return out


def list_detections(video_path, time_sec=0.0, model_path=None, confidence_threshold=0.35,
                    preferred_class_ids=None, classes_json=None, max_detections=24,
                    _run_model_fn=None):
    """Return all ONNX detections on one frame with class names (not tip-only).

    Used by Create “Show all AI tags” so nipples/breasts/etc. are visible
    alongside glans — find_roi still returns a single tip box.
    """
    model_path = model_path or default_model_path()
    registry_path = classes_json
    if not registry_path and model_path:
        registry_path = os.path.dirname(os.path.abspath(model_path))
    registry = load_class_registry(registry_path) if registry_path else {}
    id_to_name = invert_class_registry(registry)

    if _run_model_fn is None:
        session = _load_session(model_path)
        _run_model_fn = lambda frame: _run_model(session, frame)

    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"Video konnte nicht geöffnet werden: {video_path}")
    try:
        total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
        width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        fps = float(cap.get(cv2.CAP_PROP_FPS)) or 30.0
        idx = max(0, min(max(0, total - 1), int(round(float(time_sec) * fps))))
        cap.set(cv2.CAP_PROP_POS_FRAMES, idx)
        ok, frame = cap.read()
        if not ok:
            raise RuntimeError("AI detections: could not read frame")
        raw = _run_model_fn(frame)
        detections = decode_detections(raw, confidence_threshold)
        detections = _filter_preferred(detections, preferred_class_ids)
        detections = sorted(
            detections,
            key=lambda d: float(d.get("confidence", 0)),
            reverse=True,
        )[: max(1, int(max_detections))]
        out = []
        for d in detections:
            x0 = int(d["x0"] * width)
            y0 = int(d["y0"] * height)
            x1 = int(d["x1"] * width)
            y1 = int(d["y1"] * height)
            x0, x1 = sorted((max(0, x0), min(width, x1)))
            y0, y1 = sorted((max(0, y0), min(height, y1)))
            w = max(1, x1 - x0)
            h = max(1, y1 - y0)
            cid = int(d.get("class_id", -1))
            name = id_to_name.get(cid, str(cid))
            out.append({
                "x": x0, "y": y0, "w": w, "h": h,
                "confidence": float(d["confidence"]),
                "classId": cid,
                "className": name,
            })
        return out
    finally:
        cap.release()


def resolve_expected_class_id(expected_class, registry):
    """Resolve one canonical semantic class to exactly one model-specific ID.

    Unlike the legacy preferred-class helper, this is fail-closed: aliases are
    normalized, but a missing/conflicting manifest never becomes "all classes".
    """
    expected = bodyparts.normalize(expected_class)
    if not expected or not bodyparts.is_canonical(expected):
        raise StrictClassBindingError(
            "class_unresolved", f"Unknown expected body class: {expected_class!r}", expected)
    if not registry:
        raise StrictClassBindingError(
            "manifest_missing", "classes.json is required for strict AI target matching", expected)
    ids = {
        int(class_id)
        for name, class_id in registry.items()
        if bodyparts.normalize(name) == expected
    }
    if not ids:
        raise StrictClassBindingError(
            "class_unresolved",
            f"Expected class {expected!r} is not present in classes.json",
            expected,
        )
    if len(ids) != 1:
        raise StrictClassBindingError(
            "class_conflict",
            f"Expected class {expected!r} maps to conflicting model IDs: {sorted(ids)}",
            expected,
        )
    expected_id = ids.pop()
    claims = {
        bodyparts.normalize(name)
        for name, class_id in registry.items()
        if int(class_id) == expected_id
    }
    if claims != {expected}:
        raise StrictClassBindingError(
            "class_conflict",
            f"Model ID {expected_id} is shared by different classes: {sorted(claims)}",
            expected,
        )
    return expected_id


def select_strict_detection(detections, expected_class_id, frame_w, frame_h,
                            confidence_threshold=0.35, ambiguity_margin=0.08,
                            use_depth_rank=False, depth_map=None):
    """Choose only the requested class or raise a typed fail-closed error."""
    exact = [d for d in detections if int(d.get("class_id", -1)) == int(expected_class_id)]
    if not exact:
        raise StrictClassBindingError(
            "target_not_detected", "Expected body class was not detected")
    eligible = [d for d in exact if float(d.get("confidence", 0.0)) >= confidence_threshold]
    if not eligible:
        raise StrictClassBindingError(
            "below_confidence", "Expected body class was detected below the confidence threshold")
    ranked = sorted(
        eligible,
        key=lambda d: _detection_rank_score(d, frame_w, frame_h, depth_map, use_depth_rank),
        reverse=True,
    )
    best = ranked[0]
    best_score = _detection_rank_score(best, frame_w, frame_h, depth_map, use_depth_rank)
    second = next((d for d in ranked[1:] if _iou(d, best) <= 0.3), None)
    if second is not None:
        second_score = _detection_rank_score(second, frame_w, frame_h, depth_map, use_depth_rank)
        if best_score-second_score <= max(0.0, float(ambiguity_margin)):
            raise StrictClassBindingError(
                "ambiguous_target", "Two distinct target boxes are too close in confidence")
    return best, select_best_box([best], frame_w, frame_h, use_depth_rank=use_depth_rank,
                                 depth_map=depth_map)


def _preprocess_frame(frame_bgr, input_size):
    """BGR-Frame (OpenCV) -> (1,3,H,W) float32 RGB 0..1, wie im Modell-Vertrag."""
    resized = cv2.resize(frame_bgr, (input_size, input_size))
    rgb = cv2.cvtColor(resized, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
    chw = np.transpose(rgb, (2, 0, 1))
    return np.expand_dims(chw, axis=0)


def _load_session(model_path):
    try:
        import onnxruntime
    except ImportError as exc:
        raise ModelUnavailable(
            "onnxruntime ist nicht installiert - siehe generator/requirements-ai.txt"
        ) from exc
    if not os.path.isfile(model_path):
        raise ModelUnavailable(f"Kein Modell unter {model_path!r} gefunden")
    return onnxruntime.InferenceSession(model_path, providers=["CPUExecutionProvider"])


def _run_model(session, frame_bgr, input_size=640):
    """Echte Inferenz - braucht onnxruntime + Modell. Nicht unit-getestet
    (kein Modell im Repo); ai_roi_test.py prüft stattdessen decode/select
    und find_roi() mit injiziertem _run_model_fn."""
    input_name = session.get_inputs()[0].name
    tensor = _preprocess_frame(frame_bgr, input_size)
    raw = session.run(None, {input_name: tensor})[0]
    return raw


def find_roi(video_path, start_frame=0, end_frame=None, report_progress=True,
             model_path=None, confidence_threshold=0.35, sample_frames=5,
             preferred_class_ids=None, use_depth_rank=False, _run_model_fn=None):
    """Same contract as auto_roi.find_roi: returns (x,y,w,h) or raises.

    preferred_class_ids: optional list of class ids (from classes.json) so a
    multi-class ROI model prefers e.g. hand/breast over background junk.
    """
    model_path = model_path or default_model_path()
    if _run_model_fn is None:
        session = _load_session(model_path)  # raises ModelUnavailable
        _run_model_fn = lambda frame: _run_model(session, frame)

    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"Video konnte nicht geöffnet werden: {video_path}")
    try:
        total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
        width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        end = min(end_frame, total) if end_frame is not None else total
        end = max(end, start_frame + 1)
        indices = sorted(set(
            int(start_frame + i * (end - start_frame - 1) / max(1, sample_frames - 1))
            for i in range(sample_frames)
        ))

        best_overall = None
        for n, idx in enumerate(indices):
            if report_progress:
                print(f"PROGRESS {n} {len(indices)}", file=sys.stderr, flush=True)
            cap.set(cv2.CAP_PROP_POS_FRAMES, idx)
            ok, frame = cap.read()
            if not ok:
                continue
            raw = _run_model_fn(frame)
            detections = decode_detections(raw, confidence_threshold)
            detections = _filter_preferred(detections, preferred_class_ids)
            depth_map = _optional_depth_map(frame, use_depth_rank)
            for det in detections:
                if best_overall is None or _detection_rank_score(
                        det, width, height, depth_map, use_depth_rank) > _detection_rank_score(
                        best_overall, width, height, depth_map, use_depth_rank):
                    best_overall = det
        if report_progress:
            print(f"PROGRESS {len(indices)} {len(indices)}", file=sys.stderr, flush=True)
    finally:
        cap.release()

    if best_overall is None:
        raise RuntimeError(
            "KI-Regionssuche: keine Erkennung über der Konfidenzschwelle - "
            "bitte Region von Hand markieren oder --roi-finder auto verwenden")
    return select_best_box([best_overall], width, height,
                           preferred_class_ids=preferred_class_ids,
                           use_depth_rank=use_depth_rank)


def find_expected_roi(video_path, expected_class, expected_class_id,
                      start_frame=0, end_frame=None, report_progress=True,
                      model_path=None, confidence_threshold=0.35, sample_frames=1,
                      time_sec=None,
                      ambiguity_margin=0.08, use_depth_rank=False, _run_model_fn=None):
    """Return a typed proposal for exactly one expected semantic target.

    The strongest unrelated detection is never considered. Product inference
    uses one exact preview frame (``time_sec``), so a same-class instance from
    another scene cannot silently replace the object the user is looking at.
    """
    expected = bodyparts.normalize(expected_class)
    model_path = model_path or default_model_path()
    if _run_model_fn is None:
        session = _load_session(model_path)
        _run_model_fn = lambda frame: _run_model(session, frame)

    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"Video konnte nicht geöffnet werden: {video_path}")
    best = None
    best_box = None
    best_score = -1.0
    best_index = -1
    saw_target = False
    saw_ambiguous = False
    try:
        total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
        width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        if time_sec is not None:
            fps = float(cap.get(cv2.CAP_PROP_FPS)) or 30.0
            start_frame = max(
                0,
                min(max(0, total - 1), int(round(float(time_sec) * fps))),
            )
            end_frame = start_frame + 1
            sample_frames = 1
        end = min(end_frame, total) if end_frame is not None else total
        end = max(end, start_frame + 1)
        indices = sorted(set(
            int(start_frame + i * (end - start_frame - 1) / max(1, sample_frames - 1))
            for i in range(sample_frames)
        ))
        for n, idx in enumerate(indices):
            if report_progress:
                print(f"PROGRESS {n} {len(indices)}", file=sys.stderr, flush=True)
            cap.set(cv2.CAP_PROP_POS_FRAMES, idx)
            ok, frame = cap.read()
            if not ok:
                continue
            raw = _run_model_fn(frame)
            # Keep sub-threshold boxes long enough to distinguish "not present"
            # from "present but too weak" for an honest UI message.
            detections = decode_detections(raw, 0.0)
            if any(int(d.get("class_id", -1)) == int(expected_class_id) for d in detections):
                saw_target = True
            depth_map = _optional_depth_map(frame, use_depth_rank)
            try:
                det, box = select_strict_detection(
                    detections, expected_class_id, width, height,
                    confidence_threshold=confidence_threshold,
                    ambiguity_margin=ambiguity_margin,
                    use_depth_rank=use_depth_rank,
                    depth_map=depth_map,
                )
            except StrictClassBindingError as exc:
                if exc.code == "ambiguous_target":
                    saw_ambiguous = True
                continue
            score = _detection_rank_score(det, width, height, depth_map, use_depth_rank)
            if score > best_score:
                best, best_box, best_score, best_index = det, box, score, idx
        if report_progress:
            print(f"PROGRESS {len(indices)} {len(indices)}", file=sys.stderr, flush=True)
    finally:
        cap.release()

    if best is None:
        if saw_ambiguous:
            code = "ambiguous_target"
            message = f"Multiple {expected} boxes were equally plausible"
        elif saw_target:
            code = "below_confidence"
            message = f"Detected {expected}, but confidence was below {confidence_threshold:.2f}"
        else:
            code = "target_not_detected"
            message = f"No {expected} detection was found"
        raise StrictClassBindingError(code, message, expected)

    return {
        "x": best_box[0], "y": best_box[1], "w": best_box[2], "h": best_box[3],
        "expectedClass": expected,
        "matchedClass": expected,
        "matchedClassId": int(expected_class_id),
        "confidence": float(best["confidence"]),
        "sampleIndex": int(best_index),
        "match": True,
        "status": "matched",
    }


def find_expected_image(image_path, expected_class, expected_class_id,
                        model_path=None, confidence_threshold=0.35,
                        ambiguity_margin=0.08, use_depth_rank=False,
                        _run_model_fn=None):
    """Strict proposal from the exact preview image shown by the GUI."""
    expected = bodyparts.normalize(expected_class)
    model_path = model_path or default_model_path()
    if _run_model_fn is None:
        session = _load_session(model_path)
        _run_model_fn = lambda frame: _run_model(session, frame)
    frame = cv2.imread(image_path, cv2.IMREAD_COLOR)
    if frame is None:
        raise RuntimeError(f"Preview image could not be opened: {image_path}")
    height, width = frame.shape[:2]
    detections = decode_detections(_run_model_fn(frame), 0.0)
    depth_map = _optional_depth_map(frame, use_depth_rank)
    det, box = select_strict_detection(
        detections, expected_class_id, width, height,
        confidence_threshold=confidence_threshold,
        ambiguity_margin=ambiguity_margin,
        use_depth_rank=use_depth_rank,
        depth_map=depth_map,
    )
    return {
        "x": box[0], "y": box[1], "w": box[2], "h": box[3],
        "expectedClass": expected,
        "matchedClass": expected,
        "matchedClassId": int(expected_class_id),
        "confidence": float(det["confidence"]),
        "sampleIndex": -1,
        "match": True,
        "status": "matched",
    }


def find_two_rois(video_path, start_frame=0, end_frame=None, report_progress=True,
                   model_path=None, confidence_threshold=0.35, sample_frames=5,
                   preferred_class_ids=None, use_depth_rank=False, _run_model_fn=None):
    """Like find_roi(), but returns (roi1, roi2). roi2 is None when no second
    sufficiently separated object was found (see select_two_best_boxes).

    Still a human-confirmed proposal — never silently auto-committed (#8).
    """
    model_path = model_path or default_model_path()
    if _run_model_fn is None:
        session = _load_session(model_path)  # raises ModelUnavailable
        _run_model_fn = lambda frame: _run_model(session, frame)

    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"Video konnte nicht geöffnet werden: {video_path}")
    try:
        total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
        width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        end = min(end_frame, total) if end_frame is not None else total
        end = max(end, start_frame + 1)
        indices = sorted(set(
            int(start_frame + i * (end - start_frame - 1) / max(1, sample_frames - 1))
            for i in range(sample_frames)
        ))

        pooled = []
        last_frame = None
        for n, idx in enumerate(indices):
            if report_progress:
                print(f"PROGRESS {n} {len(indices)}", file=sys.stderr, flush=True)
            cap.set(cv2.CAP_PROP_POS_FRAMES, idx)
            ok, frame = cap.read()
            if not ok:
                continue
            last_frame = frame
            raw = _run_model_fn(frame)
            pooled.extend(decode_detections(raw, confidence_threshold))
        if report_progress:
            print(f"PROGRESS {len(indices)} {len(indices)}", file=sys.stderr, flush=True)
    finally:
        cap.release()

    if not pooled:
        raise RuntimeError(
            "KI-Regionssuche: keine Erkennung über der Konfidenzschwelle - "
            "bitte Regionen von Hand markieren oder --roi-finder auto verwenden")
    depth_map = _optional_depth_map(last_frame, use_depth_rank)
    return select_two_best_boxes(pooled, width, height,
                                 preferred_class_ids=preferred_class_ids,
                                 use_depth_rank=use_depth_rank, depth_map=depth_map)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--video", help="Pfad zum Video (nicht nötig mit --check/--image)")
    ap.add_argument("--image", help="Exact preview image for strict target matching")
    ap.add_argument("--model", default=None, help="Pfad zur .onnx-Datei (sonst Standardordner)")
    ap.add_argument("--confidence", type=float, default=0.35)
    ap.add_argument("--preferred-classes", default=None,
                     help="Comma-separated class names or ids to prefer "
                          "(e.g. hand,breast or 0,1). Names need --classes-json.")
    ap.add_argument("--classes-json", default=None,
                     help="Path to classes.json or its dataset directory "
                          "(default: sibling of --model, then dataset under config).")
    ap.add_argument("--expected-class", default=None,
                    help="Canonical body class required for strict target matching "
                         "(e.g. glans, nipples, mouth). Requires --strict-class.")
    ap.add_argument("--strict-class", action="store_true",
                    help="Fail closed when the expected class is absent, weak or ambiguous. "
                         "Never falls back to another class or the motion finder.")
    ap.add_argument("--ambiguity-margin", type=float, default=0.08,
                    help="Maximum score gap considered ambiguous between two distinct "
                         "detections of the expected class (default: 0.08).")
    ap.add_argument("--time-sec", type=float, default=0.0,
                    help="Exact preview time used by strict target matching. The strict "
                         "path evaluates this frame only, avoiding cross-scene instance swaps.")
    ap.add_argument("--check", action="store_true",
                     help="Nur prüfen, ob KI-Erkennung nutzbar ist (onnxruntime + Modell "
                          "vorhanden), ohne Video zu öffnen - für die GUI, um den KI-Knopf "
                          "zu aktivieren/auszublenden, ohne selbst eine Erkennung anzustoßen.")
    ap.add_argument("--two", action="store_true",
                     help="Zwei Regionen statt einer suchen (find_two_rois statt find_roi) - "
                          "für Profile mit zweiter Region (tf/tj). UNGETESTET gegen echtes "
                          "Material, siehe find_two_rois()-Docstring: bleibt ein Vorschlag, "
                          "den ein Mensch bestätigt/korrigiert, nie automatisch übernommen.")
    ap.add_argument("--list-detections", action="store_true",
                     help="List all class detections on one frame (DETECTION lines). "
                          "Shows nipples/breasts/etc., not tip-only. Does not apply a tip box.")
    args = ap.parse_args()

    if args.check:
        ok = available(args.model)
        path = args.model or default_model_path()
        print("AVAILABLE" if ok else "UNAVAILABLE")
        print(f"model_path={path}", file=sys.stderr)
        return

    if not args.video and not (args.strict_class and args.image):
        ap.error("--video ist erforderlich, außer bei --check oder strict --image")

    if args.strict_class and args.two:
        ap.error("--strict-class currently supports one expected Tip target, not --two")

    if args.list_detections:
        if not args.video:
            ap.error("--list-detections requires --video")
        preferred = None
        if args.preferred_classes:
            registry_path = args.classes_json
            if not registry_path and args.model:
                registry_path = os.path.dirname(os.path.abspath(args.model))
            registry = load_class_registry(registry_path) if registry_path else {}
            preferred = resolve_preferred_class_ids(args.preferred_classes, registry)
        try:
            dets = list_detections(
                args.video,
                time_sec=args.time_sec or 0.0,
                model_path=args.model,
                confidence_threshold=args.confidence,
                preferred_class_ids=preferred,
                classes_json=args.classes_json,
            )
        except (ModelUnavailable, RuntimeError) as exc:
            print(f"AI detections failed: {exc}", file=sys.stderr)
            sys.exit(1)
        for i, d in enumerate(dets):
            print(
                f"DETECTION {d['x']} {d['y']} {d['w']} {d['h']} "
                f"{d['confidence']:.4f} {d['classId']} {d['className']} {i}"
            )
        print(f"AI detections: {len(dets)} boxes", file=sys.stderr)
        return

    if args.strict_class:
        if not args.expected_class:
            ap.error("--expected-class is required with --strict-class")
        registry_path = args.classes_json
        if not registry_path:
            registry_path = os.path.dirname(os.path.abspath(
                args.model or default_model_path()))
        try:
            try:
                registry = load_class_registry(registry_path)
            except (OSError, ValueError, TypeError) as exc:
                raise StrictClassBindingError(
                    "manifest_invalid",
                    f"classes.json could not be read: {exc}",
                    args.expected_class,
                ) from exc
            expected_id = resolve_expected_class_id(args.expected_class, registry)
            kwargs = {
                "expected_class": args.expected_class,
                "expected_class_id": expected_id,
                "model_path": args.model,
                "confidence_threshold": args.confidence,
                "ambiguity_margin": args.ambiguity_margin,
            }
            if args.image:
                result = find_expected_image(args.image, **kwargs)
            else:
                result = find_expected_roi(args.video, time_sec=args.time_sec, **kwargs)
        except StrictClassBindingError as exc:
            payload = {
                "code": exc.code,
                "expectedClass": exc.expected_class or bodyparts.normalize(args.expected_class),
                "message": str(exc),
                "match": False,
            }
            print("TIP_ERROR " + json.dumps(payload, ensure_ascii=False, sort_keys=True))
            print(f"Strict AI target failed [{exc.code}]: {exc}", file=sys.stderr)
            sys.exit(2)
        except (ModelUnavailable, RuntimeError) as exc:
            payload = {
                "code": "detector_failed",
                "expectedClass": bodyparts.normalize(args.expected_class),
                "message": str(exc),
                "match": False,
            }
            print("TIP_ERROR " + json.dumps(payload, ensure_ascii=False, sort_keys=True))
            print(f"Strict AI target failed: {exc}", file=sys.stderr)
            sys.exit(1)
        print("TIP_DETECTION " + json.dumps(result, ensure_ascii=False, sort_keys=True))
        print("ROI {x} {y} {w} {h}".format(**result))
        print(
            f"Strict AI target found: {result['matchedClass']} "
            f"confidence={result['confidence']:.3f}",
            file=sys.stderr,
        )
        return

    preferred = None
    if args.preferred_classes:
        registry_path = args.classes_json
        if not registry_path and args.model:
            registry_path = os.path.dirname(os.path.abspath(args.model))
        registry = load_class_registry(registry_path) if registry_path else {}
        preferred = resolve_preferred_class_ids(args.preferred_classes, registry)
        if preferred is None:
            print("Warning: --preferred-classes could not be resolved "
                  "(names require --classes-json)", file=sys.stderr)

    if args.two:
        try:
            roi1, roi2 = find_two_rois(args.video, model_path=args.model,
                                        confidence_threshold=args.confidence,
                                        preferred_class_ids=preferred)
        except (ModelUnavailable, RuntimeError) as exc:
            print(f"AI region search failed: {exc}", file=sys.stderr)
            sys.exit(1)
        if roi1 is None:
            print("AI region search failed: no region found", file=sys.stderr)
            sys.exit(1)
        x, y, w, h = roi1
        print(f"ROI {x} {y} {w} {h}")
        print(f"AI region 1 found: x={x} y={y} w={w} h={h}", file=sys.stderr)
        if roi2 is not None:
            x2, y2, w2, h2 = roi2
            print(f"ROI2 {x2} {y2} {w2} {h2}")
            print(f"AI region 2 found: x={x2} y={y2} w={w2} h={h2}", file=sys.stderr)
        else:
            print("AI region 2: no second distinct object found — mark manually", file=sys.stderr)
        return

    try:
        x, y, w, h = find_roi(args.video, model_path=args.model,
                               confidence_threshold=args.confidence,
                               preferred_class_ids=preferred)
    except (ModelUnavailable, RuntimeError) as exc:
        print(f"AI region search failed: {exc}", file=sys.stderr)
        sys.exit(1)

    print(f"ROI {x} {y} {w} {h}")
    print(f"AI region found: x={x} y={y} w={w} h={h}", file=sys.stderr)


if __name__ == "__main__":
    main()
