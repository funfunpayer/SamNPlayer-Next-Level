#!/usr/bin/env python3
"""generate_funscript.py - einfacher, klassischer CV-basierter funscript-Generator.

Kein Deep Learning, keine trainierten Modelle - der Nutzer markiert per Hand
eine Bildregion (ROI) im ersten Frame, ein OpenCV-Tracker (CSRT) verfolgt sie
durchs Video, die vertikale Bewegung wird zu einer funscript-Positionskurve.

Das ist bewusst der "einfache" Ansatz (vergleichbar mit dem, was in dieser
Nische vor Deep-Learning-Ansätzen üblich war: manuelle ROI + klassischer
Tracker) - eigenständig geschrieben, keine Codeübernahme von irgendwo.
Für komplexe/verdeckte Szenen ist ein trainiertes Objekterkennungsmodell
(YOLO o.ä.) deutlich robuster, das ist hier bewusst nicht das Ziel.

Pipeline:
  1. ROI im ersten Frame per CSRT-Tracker durchs Video verfolgen -> rohe
     y-Positionskurve.
  1b. Camera Motion Compensation: globale Kamerabewegung wird aus
      Hintergrund-Features (außerhalb der ROI) per Sparse Optical Flow +
      robuster affiner Schätzung (RANSAC) gemessen und von der Rohkurve
      abgezogen, damit ein Kameraschwenk nicht als Objektbewegung
      fehlinterpretiert wird (kann per --no-camera-compensation abgeschaltet
      werden).
  2. Savitzky-Golay-Filter zur Rauschunterdrückung.
  3. Min/Max-Normalisierung auf 0-100 (funscript-Konvention: 100 = "oben").
  4. Peak/Valley-Erkennung zur Reduktion auf sinnvolle Keyframes (statt jeden
     Frame als Punkt zu speichern - das wäre unnötig groß und ruckelig).
  4b. Optionale RDP-Simplifizierung (--rdp-tolerance) als zusätzliche
      Redundanzreduktion auf den bereits gefundenen Keyframes.
  5. Schreiben als .funscript (JSON, {"actions": [{"at": ms, "pos": 0-100}]}).

Nutzung:
  python3 generate_funscript.py --video input.mp4 --roi "120,80,60,60" \\
      --output out.funscript

Ohne --roi öffnet sich ein interaktives Auswahlfenster (cv2.selectROI) auf
dem ersten Frame - erfordert eine lokale Anzeige (kein SSH ohne X-Forwarding).
"""

import argparse
import datetime
import glob
import hashlib
import heapq
import json
import os
import sys
import time

import cv2
import numpy as np
from scipy.signal import savgol_filter, find_peaks

import quality_doctor
from tf_tj_meta import (
    is_distance_profile,
    clamp_actions_pos,
    apply_profile_metadata,
)


def _punch_exclude_boxes(mask, boxes, pad=10):
    """Zero out one or more (x,y,w,h) boxes on a uint8 feature mask."""
    if not boxes:
        return mask
    h, w = mask.shape[:2]
    for box in boxes:
        if box is None:
            continue
        x, y, bw, bh = [int(v) for v in box]
        x0, y0 = max(0, x - pad), max(0, y - pad)
        x1, y1 = min(w, x + bw + pad), min(h, y + bh + pad)
        if x1 > x0 and y1 > y0:
            mask[y0:y1, x0:x1] = 0
    return mask


def estimate_camera_motion(prev_gray, gray, exclude_bbox, extra_excludes=None):
    """Estimate global vertical camera motion between two frames.

    Background features outside the tracked object region (and optional
    soft-mask body-part boxes) are tracked with sparse LK + RANSAC.
    Returns 0.0 when too few reliable background points are found.
    exclude_bbox: primary (x,y,w,h). extra_excludes: optional list of boxes
    (docs/BODY_REGIONS.md mask role).
    """
    h, w = prev_gray.shape[:2]
    mask = np.full((h, w), 255, dtype=np.uint8)
    boxes = []
    if exclude_bbox is not None:
        boxes.append(exclude_bbox)
    if extra_excludes:
        boxes.extend(extra_excludes)
    _punch_exclude_boxes(mask, boxes, pad=10)

    prev_pts = cv2.goodFeaturesToTrack(
        prev_gray, maxCorners=200, qualityLevel=0.01, minDistance=20, blockSize=7, mask=mask
    )
    if prev_pts is None or len(prev_pts) < 10:
        return 0.0

    curr_pts, status, _ = cv2.calcOpticalFlowPyrLK(prev_gray, gray, prev_pts, None)
    if curr_pts is None or status is None:
        return 0.0
    status = status.reshape(-1)
    good_prev = prev_pts[status == 1]
    good_curr = curr_pts[status == 1]
    if len(good_prev) < 10:
        return 0.0

    transform, _inliers = cv2.estimateAffinePartial2D(good_prev, good_curr, method=cv2.RANSAC)
    if transform is None:
        return 0.0
    # transform[1, 2] ist der geschätzte y-Translationsanteil (Pixel) der
    # affinen Transformation - das ist die globale Kamerabewegung.
    return float(transform[1, 2])


def frame_signature(gray):
    """Verkleinertes Graubild für den räumlichen Schnitt-Vergleich.

    Bewusst stark verkleinert: dadurch fallen Rauschen und kleine
    Objektbewegungen kaum ins Gewicht, während eine komplett andere
    Bildkomposition sofort sichtbar wird.
    """
    return cv2.resize(gray, (64, 48), interpolation=cv2.INTER_AREA).astype(np.float32)


def detect_scene_cut(prev_gray, gray, threshold=0.5, prev_signature=None,
                     signature=None, diff_threshold=12.0):
    """Erkennt einen harten Szenenschnitt zwischen zwei Frames.

    Zwei unabhängige Signale, es genügt eines:

    1. Histogramm-Korrelation. Eine schnelle Objekt- oder Kamerabewegung
       verschiebt Bildinhalte, verändert die Helligkeitsverteilung als Ganzes
       aber kaum - ein Kamerawechsel sieht meist wie eine unkorrelierte neue
       Verteilung aus.

    2. Mittlere absolute Differenz stark verkleinerter Graubilder.

    Signal 2 ist nötig, weil das Histogramm die RÄUMLICHE Anordnung gar nicht
    sieht: zwei völlig verschiedene Szenen mit ähnlicher Helligkeitsverteilung
    sind für es identisch. An einem Testvideo gemessen, bei dem zwei Szenen
    dieselbe Grundhelligkeit hatten, lag die Histogramm-Korrelation am Schnitt
    bei 0.998 - der Schnitt wurde schlicht übersehen. Die Bilddifferenz lag
    beim 10-fachen der stärksten Bewegung innerhalb einer Szene.
    """
    hist1 = cv2.calcHist([prev_gray], [0], None, [64], [0, 256])
    hist2 = cv2.calcHist([gray], [0], None, [64], [0, 256])
    cv2.normalize(hist1, hist1)
    cv2.normalize(hist2, hist2)
    if cv2.compareHist(hist1, hist2, cv2.HISTCMP_CORREL) < threshold:
        return True

    if prev_signature is None:
        prev_signature = frame_signature(prev_gray)
    if signature is None:
        signature = frame_signature(gray)
    return float(np.mean(np.abs(signature - prev_signature))) > diff_threshold


def track_roi(video_path, roi, max_frames=None, camera_compensation=True,
              scene_cut_detection=True, start_frame=0, axis="auto",
              appearance_memory=True, mask_rois=None):
    """Track roi=(x,y,w,h); return (timestamps_ms, positions, frame_size, scene_cuts, stats).

    mask_rois: optional list of (x,y,w,h) soft-exclude boxes punched out of
    the camera-motion feature mask (body-part mask role).
    """
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"Could not open video: {video_path}")

    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

    if start_frame > 0:
        # Für die szenenweise Verarbeitung: an den Szenenanfang springen,
        # statt das Video von vorne zu lesen.
        cap.set(cv2.CAP_PROP_POS_FRAMES, start_frame)

    ok, first_frame = cap.read()
    if not ok:
        cap.release()
        raise RuntimeError("Could not read first frame")

    tracker = create_tracker()
    tracker.init(first_frame, tuple(roi))

    timestamps_ms = [0]
    y_positions = [roi[1] + roi[3] / 2.0]
    # Die horizontale Mitte wird mitgeführt. Sie kostet nichts - der Tracker
    # liefert sie ohnehin - und beantwortet die Frage, an der ein Lauf sonst
    # unerklärlich scheitert: bewegt sich das Objekt überhaupt vertikal? Bei
    # überwiegend seitlicher Bewegung findet die vertikale Auswertung nichts,
    # ohne dass irgendetwas darauf hinweist. Außerdem Grundlage für
    # Multi-Achsen-Skripte.
    x_positions = [roi[0] + roi[2] / 2.0]
    camera_dy_cumulative = [0.0]
    last_bbox = tuple(roi)
    needs_gray = camera_compensation or scene_cut_detection or appearance_memory
    prev_gray = cv2.cvtColor(first_frame, cv2.COLOR_BGR2GRAY) if needs_gray else None
    camera_frames_lost = 0
    tracker_lost_frames = 0
    memory = AppearanceMemory() if appearance_memory else None
    scene_cuts = []

    frame_idx = 1
    # Fortschritt maschinenlesbar melden ("PROGRESS <erledigt> <gesamt>"),
    # damit die Oberfläche einen Balken zeigen kann - das Tracking läuft
    # sonst minutenlang ohne jedes Lebenszeichen. total_frames kann bei
    # manchen Containern 0 oder falsch sein; dann wird 0 gemeldet und die
    # GUI zeigt einen unbestimmten Fortschritt statt eines falschen Werts.
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)
    if max_frames:
        total_frames = min(total_frames, max_frames) if total_frames else max_frames
    next_report = 0
    while True:
        if frame_idx >= next_report:
            print(f"PROGRESS {frame_idx} {total_frames}", file=sys.stderr, flush=True)
            next_report = frame_idx + max(1, (total_frames or 1000) // 100)
        ok, frame = cap.read()
        if not ok:
            break

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY) if needs_gray else None
        is_cut = (scene_cut_detection and prev_gray is not None
                  and detect_scene_cut(prev_gray, gray))
        if is_cut:
            scene_cuts.append(frame_idx)
            # Hard cut: only re-anchor when appearance memory finds the tip
            # in the new scene. Never invent a tip by sticking last_bbox on
            # background (silent Ausfallcode).
            found = memory.reacquire(gray, last_bbox) if memory else None
            if found:
                tracker = create_tracker()
                tracker.init(frame, tuple(int(v) for v in found))
                ok, bbox = True, found
            else:
                ok, bbox = False, last_bbox
        else:
            ok, bbox = tracker.update(frame)
            if not ok and memory:
                # Auch bei gewöhnlichem Verlust suchen, nicht nur am Schnitt.
                found = memory.reacquire(gray, last_bbox)
                if found:
                    tracker = create_tracker()
                    tracker.init(frame, tuple(int(v) for v in found))
                    ok, bbox = True, found

        if not ok:
            # Tracker hat das Objekt verloren (Verdeckung, harte Szene) -
            # letzte bekannte Position fortschreiben statt abzubrechen. Die
            # so entstandenen Abschnitte sind erfunden, nicht gemessen:
            # deshalb werden sie gezählt und als Qualitätssignal
            # weitergereicht. Die Kurve selbst sieht in diesen Abschnitten
            # unauffällig flach aus - der Zähler ist die einzige Spur.
            tracker_lost_frames += 1
            y_positions.append(y_positions[-1])
            x_positions.append(x_positions[-1])
        else:
            x, y, w, h = bbox
            y_positions.append(y + h / 2.0)
            x_positions.append(x + w / 2.0)
            last_bbox = bbox
            # Aussehen in Abständen merken, solange der Tracker sicher läuft.
            # Jeden Frame zu speichern brächte nichts: aufeinanderfolgende
            # Frames sehen praktisch gleich aus, die Sammlung wäre voller
            # Dubletten und würde die frühere Erscheinung verdrängen.
            # Gate like Go: do not poison the bank with drifted crops.
            if memory and frame_idx % 25 == 0:
                score, known = memory.matches_original(gray, bbox)
                if not known or score >= memory.min_score:
                    memory.remember(gray, bbox)

        if camera_compensation:
            if is_cut:
                # Die Vorframe-Referenz gehört zu einer anderen Szene - eine
                # Flow-Schätzung darüber hinweg wäre bedeutungslos. Wichtig:
                # die kumulierte Basislinie wird hier auf 0 zurückgesetzt,
                # nicht fortgeführt - sonst würde die in der alten Szene
                # aufsummierte Drift fälschlich von der gesamten neuen Szene
                # abgezogen.
                camera_dy_cumulative.append(0.0)
            else:
                dy = estimate_camera_motion(prev_gray, gray, last_bbox,
                                            extra_excludes=mask_rois)
                if dy == 0.0:
                    camera_frames_lost += 1
                camera_dy_cumulative.append(camera_dy_cumulative[-1] + dy)
        else:
            camera_dy_cumulative.append(0.0)

        if needs_gray:
            prev_gray = gray

        timestamps_ms.append(int(frame_idx * 1000 / fps))
        frame_idx += 1
        if max_frames and frame_idx >= max_frames:
            break

    print(f"PROGRESS {frame_idx} {frame_idx}", file=sys.stderr, flush=True)
    cap.release()

    y_positions = np.array(y_positions)
    if camera_compensation:
        camera_dy = np.array(camera_dy_cumulative)
        # Das kumulierte Kamerasignal selbst leicht glätten, bevor es
        # abgezogen wird - die Flow-/RANSAC-Schätzung pro Frame ist nicht
        # perfekt rauschfrei, und dieses Schätzrauschen würde sonst direkt
        # in die bereinigte Objektkurve durchschlagen (kumulativ sogar
        # verstärkt, da camera_dy ein Integral ist). Szenenweise glätten,
        # nicht über die ganze Kurve auf einmal - sonst würde die Glättung
        # selbst wieder etwas Drift über eine Schnittgrenze hinweg mischen,
        # die gerade bewusst auf 0 zurückgesetzt wurde.
        segment_bounds = [0] + list(scene_cuts) + [len(camera_dy)]
        for seg_start, seg_end in zip(segment_bounds[:-1], segment_bounds[1:]):
            seg_len = seg_end - seg_start
            if seg_len >= 9:
                camera_dy[seg_start:seg_end] = savgol_filter(camera_dy[seg_start:seg_end], 9, polyorder=2)
        y_positions = y_positions - camera_dy
        if camera_frames_lost > 0:
            print(f"Kamerakompensation: {camera_frames_lost}/{frame_idx} Frames ohne "
                  "verlässliche Hintergrund-Features (unverändert übernommen)", file=sys.stderr)

    if scene_cuts:
        print(f"{len(scene_cuts)} Szenenschnitt(e) erkannt und Tracker dort neu verankert "
              f"(Frames: {scene_cuts[:10]}{'...' if len(scene_cuts) > 10 else ''})", file=sys.stderr)

    if tracker_lost_frames > 0:
        share = tracker_lost_frames / max(1, frame_idx)
        print(f"Tracker hat das Objekt in {tracker_lost_frames}/{frame_idx} Frames "
              f"({share*100:.0f}%) verloren - dort wurde die letzte bekannte Position "
              "fortgeschrieben", file=sys.stderr)

    x_positions = np.asarray(x_positions, dtype=float)
    vertical_range = float(np.ptp(y_positions)) if len(y_positions) else 0.0
    horizontal_range = float(np.ptp(x_positions)) if len(x_positions) else 0.0
    # Waagerecht und senkrecht werden immer BEIDE getrackt (kostet nichts
    # zusätzlich, siehe oben) - welche Achse ins Funscript geht, wird erst
    # hier entschieden, nicht vorab über ein starres Flag erzwungen. axis=
    # "x"/"y" bleiben als expliziter Zwang erhalten (z.B. wenn ein Nutzer
    # eine bekannt falsche Auto-Wahl korrigieren will), "auto" (Standard)
    # nimmt die Achse mit der deutlich größeren Spannweite - dieselbe
    # Schwelle, die vorher nur einen Hinweis auslöste, entscheidet jetzt
    # tatsächlich.
    axis_is_horizontal = horizontal_range > vertical_range * 1.5 and horizontal_range > 5
    if axis == "x":
        chosen_positions = x_positions
    elif axis == "y":
        chosen_positions = y_positions
    else:
        chosen_positions = x_positions if axis_is_horizontal else y_positions
        if axis_is_horizontal:
            print(f"Automatische Achsenwahl: waagerecht "
                  f"({horizontal_range:.0f}px waagerecht vs {vertical_range:.0f}px senkrecht). "
                  "Mit --axis y erzwingen, falls die senkrechte Bewegung gemeint war.",
                  file=sys.stderr)

    stats = {"tracker_lost_frames": tracker_lost_frames,
             "camera_frames_lost": camera_frames_lost,
             "total_frames": frame_idx,
             "reacquisitions": memory.reacquisitions if memory else 0,
             "failed_reacquisitions": memory.failed_reacquisitions if memory else 0,
             "vertical_range": round(vertical_range, 1),
             "horizontal_range": round(horizontal_range, 1)}
    return (np.array(timestamps_ms),
            chosen_positions,
            (width, height), scene_cuts, stats)


# --- Cache für Trackingergebnisse ---------------------------------------
#
# Das Verfolgen der ROI durchs Video ist mit Abstand der teuerste Schritt:
# Dekodieren, CSRT und Optical Flow kosten bei einem 20-Sekunden-Video
# bereits Minuten. Alles danach (Glättung, Normalisierung, Keyframes, RDP,
# Quality Doctor) rechnet auf dem Ergebnis in Sekundenbruchteilen.
#
# Wer Signalparameter ausprobieren will - oder automatisch mit anderen
# Parametern nachrechnen lassen will - müsste sonst jedes Mal das komplette
# Video erneut dekodieren. Deshalb wird das Trackingergebnis unter einem
# Schlüssel abgelegt, der alle Eingaben enthält, die es beeinflussen.
#
# ACHTUNG bei Änderungen an track_roi oder estimate_camera_motion:
# TRACK_CACHE_VERSION MUSS dann erhöht werden. Sonst liefert der Cache
# stillschweigend Ergebnisse der alten Implementierung, und man sucht den
# Fehler an der völlig falschen Stelle.
TRACK_CACHE_VERSION = 4


def default_cache_dir():
    """Plattformüblicher Cache-Ort. Der Go-Wrapper übergibt den Pfad in der
    Regel explizit, damit beide Seiten denselben verwenden."""
    local = os.environ.get("LOCALAPPDATA")
    if local:
        return os.path.join(local, "SamNPlayer", "cache")
    xdg = os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache")
    return os.path.join(xdg, "SamNPlayer")


def _track_cache_key(video_path, roi, max_frames, camera_compensation, scene_cut_detection,
                     axis="auto", appearance_memory=True):
    """Schlüssel über alles, was das Trackingergebnis beeinflusst.

    Enthält Größe und Änderungszeit der Videodatei: wird das Video ersetzt,
    passt der Schlüssel nicht mehr, auch wenn der Pfad gleich bleibt.
    Nicht enthalten sind Glättung/Normalisierung/Keyframes - die laufen erst
    NACH dem Tracking und sollen den Cache gerade nicht invalidieren.
    """
    try:
        st = os.stat(video_path)
        stamp = [st.st_size, int(st.st_mtime)]
    except OSError:
        stamp = [0, 0]
    payload = json.dumps({
        "version": TRACK_CACHE_VERSION,
        "video": os.path.abspath(video_path),
        "stamp": stamp,
        "roi": list(roi),
        "max_frames": max_frames,
        "camera_compensation": bool(camera_compensation),
        "scene_cut_detection": bool(scene_cut_detection),
        "axis": axis,
        "appearance_memory": bool(appearance_memory),
    }, sort_keys=True)
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()[:32]


def track_roi_cached(video_path, roi, max_frames=None, camera_compensation=True,
                     scene_cut_detection=True, cache_dir=None, axis="auto",
                     appearance_memory=True, start_frame=0, mask_rois=None):
    """track_roi mit Zwischenspeicherung. cache_dir=None schaltet den Cache ab."""
    if not cache_dir:
        return track_roi(video_path, roi, max_frames=max_frames,
                         camera_compensation=camera_compensation,
                         scene_cut_detection=scene_cut_detection, axis=axis,
                         appearance_memory=appearance_memory,
                         start_frame=start_frame, mask_rois=mask_rois)

    key = _track_cache_key(video_path, roi, max_frames, camera_compensation,
                           scene_cut_detection, axis, appearance_memory)
    if mask_rois:
        key = f"{key}-m{len(mask_rois)}"
    if start_frame:
        key = f"{key}-s{start_frame}"
    path = os.path.join(cache_dir, f"track-{key}.npz")

    if os.path.exists(path):
        try:
            with np.load(path, allow_pickle=False) as data:
                result = (data["timestamps_ms"], data["y_positions"],
                          (int(data["width"]), int(data["height"])),
                          data["scene_cuts"].tolist(),
                          json.loads(str(data["stats"])))
            print(f"Using cached tracking result ({path})", file=sys.stderr)
            print("PROGRESS 1 1", file=sys.stderr, flush=True)
            return result
        except Exception as exc:
            # Ein beschädigter oder unlesbarer Cache darf den Lauf nie
            # verhindern - dann eben neu rechnen.
            print(f"Cache not readable, recomputing: {exc}", file=sys.stderr)

    timestamps_ms, y_positions, frame_size, scene_cuts, stats = track_roi(
        video_path, roi, max_frames=max_frames,
        camera_compensation=camera_compensation,
        scene_cut_detection=scene_cut_detection, axis=axis,
        appearance_memory=appearance_memory, start_frame=start_frame,
        mask_rois=mask_rois)

    try:
        os.makedirs(cache_dir, exist_ok=True)
        # Erst in eine temporäre Datei, dann umbenennen: ein abgebrochener
        # Lauf hinterlässt sonst eine halbe Datei, die beim nächsten Mal als
        # gültiger Cache gelesen würde.
        tmp = path + ".tmp"
        # Als Dateiobjekt schreiben, nicht als Pfad: np.savez_compressed hängt
        # an einen Pfad ohne .npz-Endung selbstständig ".npz" an, das
        # anschließende os.replace ginge dann ins Leere und der Cache bliebe
        # stillschweigend leer.
        with open(tmp, "wb") as fh:
            np.savez_compressed(
                fh,
                timestamps_ms=np.asarray(timestamps_ms),
                y_positions=np.asarray(y_positions),
                width=np.int32(frame_size[0]),
                height=np.int32(frame_size[1]),
                scene_cuts=np.asarray(scene_cuts, dtype=np.int64),
                stats=np.array(json.dumps(stats)),
            )
        os.replace(tmp, path)
    except Exception as exc:
        print(f"Cache could not be written (non-critical): {exc}", file=sys.stderr)

    return timestamps_ms, y_positions, frame_size, scene_cuts, stats


def detect_scene_boundaries(video_path, max_frames=None, min_scene_frames=25):
    """Vorlauf, der nur die Szenengrenzen bestimmt.

    Deutlich billiger als das eigentliche Tracking: nur Graustufen-Umwandlung
    und zwei Vergleichskennzahlen pro Frame, kein CSRT und kein Optical Flow.

    Gibt eine Liste von (start_frame, end_frame) zurück. Szenen unterhalb von
    min_scene_frames werden an die vorherige angehängt - für eine halbe
    Sekunde lohnt sich keine eigene Regionssuche, und ein einzelner
    Fehlalarm soll die Verarbeitung nicht zerstückeln.
    """
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"Could not open video: {video_path}")

    boundaries = [0]
    prev_gray = None
    prev_sig = None
    idx = 0
    while True:
        if max_frames and idx >= max_frames:
            break
        ok, frame = cap.read()
        if not ok:
            break
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        sig = frame_signature(gray)
        if prev_gray is not None and detect_scene_cut(
                prev_gray, gray, prev_signature=prev_sig, signature=sig):
            boundaries.append(idx)
        prev_gray, prev_sig = gray, sig
        idx += 1
    cap.release()

    total = idx
    boundaries.append(total)
    scenes = []
    for i in range(len(boundaries) - 1):
        start, end = boundaries[i], boundaries[i + 1]
        if end - start < min_scene_frames and scenes:
            scenes[-1] = (scenes[-1][0], end)
        else:
            scenes.append((start, end))
    return scenes


def track_by_scenes(video_path, fallback_roi, max_frames=None,
                    camera_compensation=True, roi_finder=None,
                    min_scene_frames=25):
    """Verfolgt das Video szenenweise mit je EIGENER Region.

    Ohne das gilt: nach einem Szenenschnitt verankert der Tracker an der
    zuletzt bekannten Position neu. Zeigt die neue Szene das Objekt an einer
    anderen Bildstelle - der Normalfall bei geschnittenem Material -, klebt
    der Tracker ab da auf Hintergrund. Gemessen an einem Testvideo mit drei
    Szenen: Szene 1 hatte 90px Bewegungsumfang, Szene 2 und 3 nur noch 1.5px,
    und der Tracker meldete dabei NULL verlorene Frames, weil er den falschen
    Bildausschnitt zuverlässig verfolgte. Der Quality Doctor gab dem
    Ergebnis 1.00.

    Jede Szene wird einzeln normalisiert zurückgegeben (siehe scene_ranges im
    Rückgabewert), weil die Regionen verschiedener Szenen in ganz
    unterschiedlichen Bildbereichen liegen - eine gemeinsame Skala über alle
    Szenen hinweg wäre bedeutungslos.
    """
    scenes = detect_scene_boundaries(video_path, max_frames=max_frames,
                                     min_scene_frames=min_scene_frames)
    print(f"{len(scenes)} Szene(n) erkannt", file=sys.stderr)

    if roi_finder is None:
        import auto_roi

        def roi_finder(path, start, end):
            return auto_roi.find_roi(path, start_frame=start, end_frame=end,
                                     report_progress=False)

    all_ts, all_y, all_stats = [], [], []
    scene_ranges = []
    frame_size = None
    offset = 0

    for scene_idx, (start, end) in enumerate(scenes):
        try:
            found = roi_finder(video_path, start, end)
        except Exception as exc:
            # Fail closed: do not silently reuse the previous scene's tip
            # (wrong latch on a new shot). Skip this scene instead.
            print(f"Szene {scene_idx + 1}: Regionssuche fehlgeschlagen ({exc}) — "
                  "Szene übersprungen (kein Tip-Reuse)", file=sys.stderr)
            continue
        if found:
            roi = found
        elif scene_idx == 0 and fallback_roi:
            # First scene only: user-provided seed when finder returns nothing.
            roi = fallback_roi
        else:
            print(f"Szene {scene_idx + 1}: keine Region — Szene übersprungen "
                  "(kein Tip-Reuse)", file=sys.stderr)
            continue

        print(f"Szene {scene_idx + 1}/{len(scenes)}: Frames {start}-{end}, Region {roi}",
              file=sys.stderr)
        ts, y, size, _, stats = track_roi(
            video_path, roi, max_frames=end - start,
            camera_compensation=camera_compensation,
            scene_cut_detection=False,       # Grenzen stehen bereits fest
            start_frame=start)
        if len(ts) == 0:
            continue
        frame_size = size
        scene_ranges.append((len(all_y), len(all_y) + len(y)))
        ts = np.asarray(ts, dtype=float)
        # Einen Frameabstand aufschlagen, sonst trägt der erste Frame der
        # neuen Szene denselben Zeitstempel wie der letzte der alten - das
        # ergibt einen doppelten Zeitstempel im Skript.
        step = float(np.median(np.diff(ts))) if len(ts) > 1 else 40.0
        all_ts.extend((ts + offset).tolist())
        all_y.extend(np.asarray(y).tolist())
        all_stats.append(stats)
        offset = all_ts[-1] + step
        fallback_roi = roi

    if not all_ts:
        raise RuntimeError("No usable scenes found")

    merged = {
        "tracker_lost_frames": sum(s["tracker_lost_frames"] for s in all_stats),
        "camera_frames_lost": sum(s["camera_frames_lost"] for s in all_stats),
        "total_frames": sum(s["total_frames"] for s in all_stats),
        "scenes": len(scene_ranges),
    }
    cuts = [r[0] for r in scene_ranges[1:]]
    return (np.array(all_ts), np.array(all_y), frame_size, cuts, merged, scene_ranges)


def refine_keyframes(timestamps_ms, pos, base_idx, max_error=6.0, max_extra_ratio=2.0):
    """Ergänzt Keyframes dort, wo die Kurve sonst schlecht wiedergegeben wird.

    Peak/Valley-Erkennung behält nur lokale Extrema. Zwischen zwei Extrema
    wird beim Abspielen linear interpoliert - was bei einem symmetrischen
    Auf und Ab passt, bei asymmetrischen Bewegungen aber nicht. Gemessen an
    einem Hub mit schnellem Anstieg und langsamem Absinken: die Rekonstruktion
    aus reinen Extrema lag um bis zu 28 von 100 Punkten daneben.

    Verfahren: zwischen zwei benachbarten Keyframes wird der Punkt mit der
    größten Abweichung zur Verbindungslinie gesucht. Liegt sie über
    max_error, wird er als Keyframe aufgenommen und beide Teilstücke erneut
    geprüft. Dadurch entstehen automatisch dort dichte Punkte, wo sich die
    Kurve stark krümmt, und dünne, wo sie flach verläuft.

    Das ist dieselbe Idee wie RDP, nur andersherum: RDP entfernt Punkte aus
    einer dichten Kurve, das hier fügt Punkte zu einer dünnen hinzu. Beides
    lässt sich kombinieren - RDP läuft danach weiter als Aufräumschritt.

    max_extra_ratio begrenzt das Ergebnis auf ein Vielfaches der
    ursprünglichen Keyframe-Anzahl, damit ein verrauschtes Signal nicht in
    tausende Punkte ausartet.
    """
    if max_error <= 0 or len(base_idx) < 2:
        return base_idx

    limit = int(len(base_idx) * max_extra_ratio)
    result = sorted(set(base_idx))

    def worst_point(start, end):
        """Punkt mit der größten Abweichung zur Verbindungslinie, oder None."""
        if end - start < 2:
            return None
        seg_t = timestamps_ms[start:end + 1]
        seg_p = pos[start:end + 1]
        span = seg_t[-1] - seg_t[0]
        if span <= 0:
            return None
        line = seg_p[0] + (seg_p[-1] - seg_p[0]) * (seg_t - seg_t[0]) / span
        deviation = np.abs(seg_p - line)
        offset = int(np.argmax(deviation))
        if offset == 0 or offset == len(seg_p) - 1 or deviation[offset] <= max_error:
            return None
        return float(deviation[offset]), start + offset

    # Segmente nach Abweichung priorisiert abarbeiten, nicht als Stapel:
    # Das Punktebudget ist begrenzt, also muss es dort ausgegeben werden, wo
    # der Fehler am größten ist. Mit einem Stapel hing das Ergebnis von der
    # Abarbeitungsreihenfolge ab - eine strengere Toleranz konnte dadurch ein
    # SCHLECHTERES Ergebnis liefern als eine lockere, weil das Budget in den
    # zuerst betrachteten Segmenten aufgebraucht wurde.
    queue = []
    for i in range(len(result) - 1):
        found = worst_point(result[i], result[i + 1])
        if found:
            heapq.heappush(queue, (-found[0], result[i], result[i + 1], found[1]))

    added = []
    while queue and len(result) + len(added) < limit:
        _, start, end, split = heapq.heappop(queue)
        added.append(split)
        for a, b in ((start, split), (split, end)):
            found = worst_point(a, b)
            if found:
                heapq.heappush(queue, (-found[0], a, b, found[1]))

    return sorted(set(result) | set(added))


def limit_speed(actions, max_units_per_second):
    """Begrenzt die Positionsänderung zwischen zwei Actions.

    Ein Gerät kann seine Position nicht beliebig schnell ändern. Verlangt das
    Skript einen Sprung, der schneller ist als das Gerät fahren kann, wird
    daraus keine schnellere Bewegung - der Hub wird schlicht abgeschnitten,
    weil das Gerät den Zielwert nie erreicht, bevor der nächste kommt. Das
    Ergebnis fühlt sich schwächer an als ein Skript, das die Grenze einhält.

    Die Amplitude wird deshalb reduziert statt der Zeitpunkt verschoben:
    Zeitpunkte zu verschieben würde die Synchronität zum Video zerstören,
    und die ist wichtiger als der letzte Punkt Auslenkung.

    Arbeitet vorwärts, weil jede Korrektur den Ausgangspunkt für die nächste
    verändert.
    """
    if not max_units_per_second or max_units_per_second <= 0 or len(actions) < 2:
        return actions, 0

    limited = [dict(actions[0])]
    changed = 0
    for action in actions[1:]:
        dt_ms = action["at"] - limited[-1]["at"]
        if dt_ms <= 0:
            limited.append(dict(action))
            continue
        max_delta = max_units_per_second * dt_ms / 1000.0
        delta = action["pos"] - limited[-1]["pos"]
        if abs(delta) > max_delta:
            new_pos = limited[-1]["pos"] + (max_delta if delta > 0 else -max_delta)
            limited.append({"at": action["at"],
                            "pos": int(round(min(100.0, max(0.0, new_pos))))})
            changed += 1
        else:
            limited.append(dict(action))
    return limited, changed


# --- Messbericht und Rückmeldung ----------------------------------------
#
# Sämtliche Schwellen im Quality Doctor sind bisher an synthetischen
# Testvideos bestimmt - saubere Sinusbewegungen, deren Wahrheit per
# Konstruktion bekannt war. Echtes Material ist unregelmäßiger und liegt
# systematisch niedriger. Um die Schwellen nachzuziehen, braucht es zwei
# Dinge: die Kennzahlen echter Läufe UND ein menschliches Urteil dazu.
# Ohne das Urteil sind die Zahlen wertlos, weil niemand weiß, welche davon
# zu einem brauchbaren Ergebnis gehören.
#
# Format ist JSON Lines: eine Zeile pro Lauf, anhängend geschrieben. Das
# übersteht Abbrüche mitten im Stapelbetrieb, ohne die bisherigen Zeilen zu
# verlieren - anders als eine einzelne große JSON-Datei, die bei jedem
# Schreiben komplett neu erzeugt werden müsste.
REPORT_VERDICTS = ("brauchbar", "grenzwertig", "unbrauchbar")


def write_report(path, record):
    """Hängt einen Messbericht als JSON-Zeile an."""
    directory = os.path.dirname(os.path.abspath(path))
    if directory:
        os.makedirs(directory, exist_ok=True)
    with open(path, "a", encoding="utf-8") as fh:
        fh.write(json.dumps(record, ensure_ascii=False) + "\n")


def read_report(path):
    """Liest alle Zeilen. Kaputte Zeilen werden übersprungen statt zu
    scheitern - eine unvollständige letzte Zeile nach einem Absturz soll
    nicht den gesamten bisherigen Bericht unlesbar machen."""
    records = []
    if not os.path.exists(path):
        return records
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return records


def add_feedback(path, output_path, verdict, comment=""):
    """Trägt ein Urteil zum zuletzt passenden Lauf nach.

    Die Datei wird komplett neu geschrieben, weil eine Zeile mittendrin
    ersetzt wird. Erst in eine temporäre Datei, dann umbenennen - ein
    Abbruch mittendrin darf den Bericht nicht zerstören.
    """
    if verdict not in REPORT_VERDICTS:
        raise ValueError(f"Urteil muss eines von {REPORT_VERDICTS} sein")
    records = read_report(path)
    target = os.path.abspath(output_path)
    updated = 0
    # Rückwärts, damit bei mehreren Läufen zum selben Video der jüngste
    # das Urteil bekommt.
    for record in reversed(records):
        if record.get("output") == target and record.get("feedback") is None:
            record["feedback"] = {
                "verdict": verdict,
                "comment": comment,
                "at": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
            }
            updated = 1
            break
    if not updated:
        return 0
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        for record in records:
            fh.write(json.dumps(record, ensure_ascii=False) + "\n")
    os.replace(tmp, path)
    return 1


def summarize_report(path):
    """Fasst den Bericht nach Urteil zusammen - die Ansicht, für die der
    ganze Bericht existiert: welche Messwerte gehören zu brauchbaren, welche
    zu unbrauchbaren Ergebnissen?"""
    records = read_report(path)
    with_feedback = [r for r in records if r.get("feedback")]
    lines = [f"{len(records)} Läufe, davon {len(with_feedback)} mit Urteil"]
    if not with_feedback:
        lines.append("Noch keine Urteile - ohne sie lassen sich die Schwellen "
                     "nicht nachziehen.")
        return "\n".join(lines)

    keys = ("concentration", "motion_range_fraction", "tracker_lost_fraction",
            "actions_per_minute")
    for verdict in REPORT_VERDICTS:
        group = [r for r in with_feedback if r["feedback"]["verdict"] == verdict]
        if not group:
            continue
        lines.append(f"\n{verdict} ({len(group)}):")
        for key in keys:
            values = [r["quality"]["metrics"].get(key) for r in group]
            values = [v for v in values if v is not None]
            if not values:
                continue
            lines.append(f"  {key:24s} min {min(values):8.3f}  median "
                         f"{sorted(values)[len(values) // 2]:8.3f}  max {max(values):8.3f}")
        scores = [r["quality"]["score"] for r in group]
        agree = sum(1 for r in group
                    if r["quality"]["passed"] == (verdict == "brauchbar"))
        lines.append(f"  {'score':24s} min {min(scores):8.2f}  median "
                     f"{sorted(scores)[len(scores) // 2]:8.2f}  max {max(scores):8.2f}")
        lines.append(f"  Urteil der Engine stimmt überein: {agree}/{len(group)}")
    return "\n".join(lines)


def describe_hardware():
    """Kurzbericht, welche Beschleunigung tatsächlich zur Verfügung steht."""
    lines = []
    try:
        cuda = cv2.cuda.getCudaEnabledDeviceCount() if hasattr(cv2, "cuda") else 0
    except Exception:
        cuda = 0
    lines.append(f"OpenCV {cv2.__version__}, CPU-Kerne {os.cpu_count()}")
    if cuda > 0:
        lines.append(f"CUDA: {cuda} Gerät(e) - dieser OpenCV-Build kann die NVIDIA-Karte nutzen")
    else:
        lines.append("CUDA: nicht verfügbar. Die pip-Pakete opencv-python und "
                     "opencv-contrib-python werden OHNE CUDA gebaut - dafür müsste OpenCV "
                     "selbst kompiliert werden.")
    if cv2.ocl.haveOpenCL():
        lines.append("OpenCL: available — Python path enables it automatically when "
                     "useful (optical flow / UMat); log line only, no GUI toggle")
    else:
        lines.append("OpenCL: not available (no drivers/GPU detected)")
    return "\n".join(lines)


def enable_opencl():
    """Try OpenCL acceleration when present; always log the outcome.

    Pip OpenCV builds ship without CUDA. OpenCL is the only GPU path they
    offer: suitable ops move to UMat, otherwise silent CPU fallback — results
    match within 1e-3. Product path is Go CSRT (no OpenCL switch); this runs
    only when Python still handles a job. No checkbox: enable when available,
    log "OpenCL active" / "not available".
    """
    if not cv2.ocl.haveOpenCL():
        print("OpenCL not available — computing on CPU", file=sys.stderr)
        return False
    cv2.ocl.setUseOpenCL(True)
    active = cv2.ocl.useOpenCL()
    print(f"OpenCL {'active' if active else 'could not be enabled'}", file=sys.stderr)
    return active


def configure_threads(threads):
    """Setzt die Anzahl der von OpenCV genutzten Threads.

    0 bedeutet: OpenCV entscheidet selbst (Standard). Ein niedriger Wert ist
    sinnvoll, wenn mehrere Videos parallel verarbeitet werden - sonst
    konkurrieren die Prozesse um dieselben Kerne und werden zusammen
    langsamer als einzeln.
    """
    if threads and threads > 0:
        cv2.setNumThreads(threads)
    return cv2.getNumThreads()


def _batch_worker(payload):
    """Verarbeitet ein Video in einem eigenen Prozess.

    Bekommt ein Dict statt eines Namespace, weil argparse.Namespace zwar
    picklebar ist, ein Dict aber unabhängig von der argparse-Version bleibt.

    Der Messbericht wird pro Prozess in eine eigene Teildatei geschrieben und
    hinterher zusammengeführt: gleichzeitiges Anhängen mehrerer Prozesse an
    dieselbe Datei ist unter Windows nicht verlässlich atomar und würde
    einzelne Zeilen ineinanderschieben.
    """
    values, threads = payload
    args = argparse.Namespace(**values)
    configure_threads(threads)
    if not getattr(args, "no_opencl", False):
        enable_opencl()
    try:
        process_one(args, None)
        return (args.video, True, "")
    except Exception as exc:
        return (args.video, False, str(exc))


def _run_batch_parallel(args, pending, jobs, per_process_threads):
    import concurrent.futures

    tasks = []
    for index, (video, output) in enumerate(pending):
        values = dict(vars(args))
        values["batch"] = None
        values["video"] = video
        values["output"] = output
        if values.get("report"):
            values["report"] = f"{values['report']}.part{index}"
        tasks.append((values, per_process_threads))

    done = failed = 0
    with concurrent.futures.ProcessPoolExecutor(max_workers=jobs) as pool:
        futures = {pool.submit(_batch_worker, t): t[0]["video"] for t in tasks}
        completed = 0
        for future in concurrent.futures.as_completed(futures):
            completed += 1
            video, ok, message = future.result()
            name = os.path.basename(video)
            if ok:
                done += 1
                print(f"[{completed}/{len(tasks)}] {name}: fertig", file=sys.stderr)
            else:
                failed += 1
                print(f"[{completed}/{len(tasks)}] {name}: FEHLER: {message}", file=sys.stderr)

    # Teilberichte zusammenführen und aufräumen.
    if args.report:
        for index in range(len(tasks)):
            part = f"{args.report}.part{index}"
            if not os.path.exists(part):
                continue
            with open(part, encoding="utf-8") as src:
                content = src.read()
            if content:
                with open(args.report, "a", encoding="utf-8") as dst:
                    dst.write(content)
            os.remove(part)

    return done, failed


VIDEO_EXTENSIONS = (".mp4", ".mkv", ".avi", ".mov", ".m4v", ".webm", ".wmv", ".mpg", ".mpeg")


def run_batch(args, parser):
    """Verarbeitet alle Videos eines Ordners nacheinander.

    Bewusst als Schleife über einzelne Läufe im selben Prozess statt als
    Neustart pro Video: der Import von OpenCV kostet mehr als die eigentliche
    Verarbeitung eines kurzen Clips.

    Ein Fehler bei einem Video beendet den Stapel NICHT. Bei zwanzig Videos
    über Nacht ist ein Abbruch beim dritten wegen einer defekten Datei der
    schlechteste mögliche Ausgang.
    """
    folder = args.batch
    if not os.path.isdir(folder):
        print(f"Error: {folder} is not a folder", file=sys.stderr)
        sys.exit(1)

    videos = sorted(
        path for path in glob.glob(os.path.join(folder, "*"))
        if os.path.splitext(path)[1].lower() in VIDEO_EXTENSIONS
    )
    if not videos:
        print(f"Keine Videos in {folder} gefunden "
              f"(gesucht: {', '.join(VIDEO_EXTENSIONS)})", file=sys.stderr)
        sys.exit(1)

    print(f"Stapelverarbeitung: {len(videos)} Video(s) in {folder}", file=sys.stderr)
    if args.backend not in ("flow", "region_fusion_auto") and not args.roi:
        print("Hinweis: ohne --roi wird im Stapelbetrieb --backend flow oder "
              "region_fusion_auto empfohlen - sonst müsste für jedes Video von Hand "
              "eine Region markiert werden.", file=sys.stderr)

    pending = []
    skipped = 0
    for video in videos:
        output = os.path.splitext(video)[0] + ".funscript"
        if os.path.exists(output):
            print(f"  {os.path.basename(video)}: übersprungen (Skript existiert bereits)",
                  file=sys.stderr)
            skipped += 1
            continue
        pending.append((video, output))

    jobs = args.jobs if args.jobs and args.jobs > 0 else 1
    jobs = max(1, min(jobs, len(pending) or 1, os.cpu_count() or 1))

    if jobs > 1:
        # Mehrere Videos gleichzeitig. Das ist der mit Abstand größte
        # Geschwindigkeitsgewinn auf einem Mehrkernrechner: die Verarbeitung
        # eines einzelnen Videos ist weitgehend sequenziell, mehrere Videos
        # sind dagegen völlig unabhängig voneinander.
        #
        # Dabei wird die OpenCV-Threadzahl pro Prozess begrenzt, sonst
        # versucht jeder Prozess alle Kerne zu belegen und alle zusammen
        # werden langsamer als einzeln.
        per_process_threads = max(1, (os.cpu_count() or 1) // jobs)
        print(f"Verarbeite {len(pending)} Video(s) mit {jobs} parallelen Prozessen "
              f"({per_process_threads} OpenCV-Thread(s) je Prozess)", file=sys.stderr)
        done, failed = _run_batch_parallel(args, pending, jobs, per_process_threads)
    else:
        done = failed = 0
        for index, (video, output) in enumerate(pending, 1):
            print(f"\n[{index}/{len(pending)}] {os.path.basename(video)}", file=sys.stderr)
            single = argparse.Namespace(**vars(args))
            single.batch = None
            single.video = video
            single.output = output
            try:
                process_one(single, parser)
                done += 1
            except KeyboardInterrupt:
                print("\nAbgebrochen.", file=sys.stderr)
                break
            except Exception as exc:
                print(f"  FEHLER: {exc}", file=sys.stderr)
                failed += 1

    print(f"\nFertig: {done} erzeugt, {skipped} übersprungen, {failed} fehlgeschlagen",
          file=sys.stderr)
    if args.report:
        print(f"Messwerte in {args.report} - Urteile nachtragen mit --feedback, "
              "Auswertung mit --report-summary", file=sys.stderr)


class AppearanceMemory:
    """Erscheinungsgedächtnis zur Wiederauffindung der verfolgten Region.

    Das ist der Versuch, die EINE Fähigkeit klassisch nachzubilden, die ein
    Objekterkenner praktisch liefert: "dieses Objekt ist jetzt hier, auch
    wenn es sich bewegt hat". Alles andere in der Pipeline kommt ohne Modell
    aus.

    Verfahren: solange der Tracker sicher läuft, werden in Abständen kleine
    Graustufen-Ausschnitte der Region gespeichert. Geht das Ziel verloren
    oder kommt ein Szenenschnitt, wird nicht blind an der letzten Position
    neu verankert - stattdessen wird das ganze Bild per Template Matching
    gegen die gespeicherten Ausschnitte abgeglichen und die beste Fundstelle
    genommen.

    Das adressiert einen gemessenen Fehlerfall: an einem Testvideo mit drei
    Szenen, in denen das Objekt jeweils woanders lag, klebte der Tracker ab
    dem ersten Schnitt auf Hintergrund - Szene 1 hatte 90px Bewegungsumfang,
    Szene 2 und 3 nur noch 1.5px. Und er meldete dabei KEINEN Verlust, weil
    er den falschen Ausschnitt zuverlässig verfolgte.

    Bewusste Beschränkungen:

    * Mehrere Ausschnitte statt eines, weil sich das Aussehen über das Video
      ändert. Ein einziges Referenzbild vom Videoanfang passt nach zwei
      Minuten oft nicht mehr.
    * Unterhalb einer Mindestübereinstimmung wird NICHT neu verankert. Eine
      geratene Position ist schlechter als die alte: sie sieht wie eine
      Messung aus, ist aber keine.
    """

    def __init__(self, max_templates=8, min_score=0.55, downscale=0.5):
        self.max_templates = max_templates
        self.min_score = min_score
        self.downscale = downscale
        self.templates = []
        self.reacquisitions = 0
        self.failed_reacquisitions = 0

    def _prepare(self, image):
        if self.downscale != 1.0:
            image = cv2.resize(image, None, fx=self.downscale, fy=self.downscale,
                               interpolation=cv2.INTER_AREA)
        return image

    def remember(self, gray, box):
        """Merkt sich das aktuelle Aussehen der Region."""
        x, y, w, h = [int(v) for v in box]
        x, y = max(0, x), max(0, y)
        patch = gray[y:y + h, x:x + w]
        if patch.size == 0 or patch.shape[0] < 8 or patch.shape[1] < 8:
            return
        small = self._prepare(patch)
        if small.shape[0] < 4 or small.shape[1] < 4:
            return
        self.templates.append(small)
        if len(self.templates) > self.max_templates:
            # Den ältesten verwerfen, aber den ERSTEN behalten: er stammt aus
            # der vom Nutzer bestätigten Startregion und ist damit der einzige
            # Ausschnitt, von dem sicher ist, dass er das Richtige zeigt.
            del self.templates[1]

    def matches_original(self, gray, box):
        """(score, known) vs templates[0] — gate remember/reacquire like Go."""
        if not self.templates:
            return 0.0, False
        orig = self.templates[0]
        scale = 1.0 / self.downscale if self.downscale != 1.0 else 1.0
        orig_w = int(orig.shape[1] * scale)
        orig_h = int(orig.shape[0] * scale)
        search_w = orig_w * 2 + 16
        search_h = orig_h * 2 + 16
        x, y, w, h = [int(v) for v in box]
        cx, cy = x + w // 2, y + h // 2
        x0 = max(0, cx - search_w // 2)
        y0 = max(0, cy - search_h // 2)
        x1 = min(gray.shape[1], x0 + search_w)
        y1 = min(gray.shape[0], y0 + search_h)
        region = gray[y0:y1, x0:x1]
        if region.size == 0:
            return 0.0, False
        frame = self._prepare(region)
        if orig.shape[0] >= frame.shape[0] or orig.shape[1] >= frame.shape[1]:
            return 0.0, False
        result = cv2.matchTemplate(frame, orig, cv2.TM_CCOEFF_NORMED)
        _, score, _, _ = cv2.minMaxLoc(result)
        return float(score), True

    def reacquire(self, gray, box_size):
        """Sucht die Region im ganzen Bild. Gibt (x, y, w, h) oder None.

        Fail-closed: candidate must resemble templates[0] when present
        (same gate as Go appearanceMemory.reacquire) — no silent latch onto
        the globally best wrong patch.
        """
        if not self.templates:
            return None
        frame = self._prepare(gray)
        best_score, best_loc, best_shape = -1.0, None, None
        for template in self.templates:
            if template.shape[0] >= frame.shape[0] or template.shape[1] >= frame.shape[1]:
                continue
            result = cv2.matchTemplate(frame, template, cv2.TM_CCOEFF_NORMED)
            _, score, _, loc = cv2.minMaxLoc(result)
            if score > best_score:
                best_score, best_loc, best_shape = score, loc, template.shape

        if best_loc is None or best_score < self.min_score:
            self.failed_reacquisitions += 1
            return None

        scale = 1.0 / self.downscale if self.downscale != 1.0 else 1.0
        x = int(best_loc[0] * scale)
        y = int(best_loc[1] * scale)
        w = int(best_shape[1] * scale)
        h = int(best_shape[0] * scale)
        cand = (x, y, max(8, w), max(8, h))
        seed_score, known = self.matches_original(gray, cand)
        if known and seed_score < self.min_score:
            self.failed_reacquisitions += 1
            return None
        self.reacquisitions += 1
        return cand


def _parse_bandpass_hz(value):
    """Parse --bandpass-hz LOW,HIGH into (low, high) floats, or None."""
    if not value:
        return None
    parts = str(value).replace(" ", "").split(",")
    if len(parts) != 2:
        raise ValueError("--bandpass-hz erwartet LOW,HIGH (z.B. 0.5,4)")
    return (float(parts[0]), float(parts[1]))


def _bandpass_1pole(values, sample_hz, low_hz, high_hz):
    """Zero-phase one-pole bandpass (forward+backward), FunGen/Flow-style."""
    x = np.asarray(values, dtype=float).copy()
    if sample_hz <= 0 or len(x) < 8:
        return x

    def _lpf(sig, cutoff):
        if cutoff is None or cutoff <= 0 or cutoff >= sample_hz / 2:
            return sig
        dt = 1.0 / sample_hz
        rc = 1.0 / (2 * np.pi * cutoff)
        alpha = dt / (rc + dt)
        y = np.empty_like(sig)
        y[0] = sig[0]
        for i in range(1, len(sig)):
            y[i] = y[i - 1] + alpha * (sig[i] - y[i - 1])
        return y

    def _hpf(sig, cutoff):
        if cutoff is None or cutoff <= 0 or cutoff >= sample_hz / 2:
            return sig
        dt = 1.0 / sample_hz
        rc = 1.0 / (2 * np.pi * cutoff)
        alpha = rc / (rc + dt)
        y = np.empty_like(sig)
        y[0] = sig[0]
        for i in range(1, len(sig)):
            y[i] = alpha * (y[i - 1] + sig[i] - sig[i - 1])
        return y

    def _filtfilt(sig, fn):
        fwd = fn(sig)
        return fn(fwd[::-1])[::-1]

    lo = float(low_hz) if low_hz else 0.0
    hi = float(high_hz) if high_hz else 0.0
    if lo > 0 and hi > 0 and lo > hi:
        lo, hi = hi, lo
    if lo > 0:
        x = _filtfilt(x, lambda s: _hpf(s, lo))
    if hi > 0:
        x = _filtfilt(x, lambda s: _lpf(s, hi))
    return x


def dynamic_range_normalize(values, window, max_gain=5.0, min_local_span=0.12):
    """Gleitende Normalisierung: hebt schwache Abschnitte auf nutzbare Stärke.

    Eine globale Normalisierung legt EINE Skala über das ganze Video. Ein
    Abschnitt mit schwächerer Bewegung bleibt dadurch dauerhaft schwach,
    auch wenn dort dieselbe Bewegung stattfindet, nur mit geringerer
    Auslenkung im Bild.

    An einem echten Skript gemessen: in der Hälfte aller 6-Sekunden-Fenster
    wurden nur 30 von 100 Punkten genutzt. Im Vergleich mit einem Skript aus
    einem etablierten Fremdprogramm für denselben Film war die mittlere
    Bewegungsstärke dadurch rund viermal geringer - bei gleicher Anzahl
    Actions und gleichem global genutztem Wertebereich. Das Skript zappelte
    in der Mitte, statt zwischen den Extremen zu wechseln.

    Zwei Sicherungen, ohne die das Verfahren schadet:

    * **max_gain** begrenzt die Verstärkung. Ohne Deckel würde ein
      praktisch unbewegter Abschnitt auf den vollen Bereich aufgeblasen -
      genau der Fehler, den die Amplitudenprüfung im Quality Doctor sonst
      meldet, hier selbst erzeugt.
    * **min_local_span**: Abschnitte, deren Auslenkung unter diesem Anteil
      der Gesamtauslenkung liegt, werden gar nicht verstärkt. Dort ist
      schlicht keine Bewegung, und Rauschen lauter zu drehen macht daraus
      keine.
    """
    values = np.asarray(values, dtype=float)
    if window < 8 or len(values) < window * 2:
        return values

    global_span = float(np.ptp(values))
    if global_span < 1e-9:
        return values

    # Lokale Mitte und Auslenkung über gleitende Fenster, danach geglättet -
    # sonst springt die Verstärkung an den Fenstergrenzen und erzeugt
    # Stufen in der Kurve.
    half = window // 2
    padded = np.pad(values, half, mode="edge")
    centers = np.empty(len(values))
    spans = np.empty(len(values))
    for i in range(len(values)):
        segment = padded[i:i + window]
        lo, hi = float(np.percentile(segment, 5)), float(np.percentile(segment, 95))
        centers[i] = (lo + hi) / 2.0
        spans[i] = hi - lo

    smooth = max(3, window // 2)
    kernel = np.ones(smooth) / smooth
    centers = np.convolve(np.pad(centers, smooth, mode="edge"), kernel, "same")[smooth:-smooth]
    spans = np.convolve(np.pad(spans, smooth, mode="edge"), kernel, "same")[smooth:-smooth]

    gains = np.ones(len(values))
    usable = spans > global_span * min_local_span
    gains[usable] = np.clip(global_span / np.maximum(spans[usable], 1e-9), 1.0, max_gain)

    return centers + (values - centers) * gains


def enforce_min_interval(timestamps_ms, pos, keyframe_idx, min_interval_ms):
    """Erzwingt einen Mindestabstand zwischen Keyframes.

    Grund: Launchcontrol verwendet beim Senden ans Gerät eine Schwelle von
    100ms; Actions, die dichter aufeinanderfolgen, kann das Gerät nicht mehr
    einzeln ausführen, und der betroffene Abschnitt gerät aus dem Takt.
    Solche Punkte sind also nicht nur überflüssig, sie schaden.

    Sie entstehen bei uns systematisch: Hoch- und Tiefpunkte werden GETRENNT
    gesucht, der Mindestabstand gilt damit nur innerhalb jeder Gruppe, nicht
    zwischen einem Hochpunkt und dem unmittelbar folgenden Tiefpunkt. Bei
    einem verrauschten Testsignal lagen dadurch 45% der Actions unter 100ms.

    Welcher von zwei zu dichten Punkten entfernt wird, entscheidet dieselbe
    Kennzahl wie bei der Keyframe-Verdichtung: der Punkt mit der kleineren
    Abweichung zur Verbindungslinie seiner Nachbarn. Der wichtigere Punkt -
    typischerweise der eigentliche Scheitel - bleibt also erhalten. Einfach
    jeden zweiten zu verwerfen würde die Amplitude beschädigen.

    Erster und letzter Punkt bleiben immer: sie begrenzen das Skript.
    """
    if min_interval_ms <= 0 or len(keyframe_idx) < 3:
        return keyframe_idx

    times = np.asarray(timestamps_ms, dtype=float)
    values = np.asarray(pos, dtype=float)
    result = list(keyframe_idx)

    def importance(position):
        """Abweichung des Punktes von der Linie zwischen seinen Nachbarn."""
        if position <= 0 or position >= len(result) - 1:
            return float("inf")   # Randpunkte nie entfernen
        left, middle, right = result[position - 1], result[position], result[position + 1]
        span = times[right] - times[left]
        if span <= 0:
            return 0.0
        expected = values[left] + (values[right] - values[left]) * \
            (times[middle] - times[left]) / span
        return abs(values[middle] - expected)

    changed = True
    while changed and len(result) > 2:
        changed = False
        for i in range(len(result) - 1):
            if times[result[i + 1]] - times[result[i]] >= min_interval_ms:
                continue
            # Von den beiden zu dicht liegenden Punkten den unwichtigeren
            # verwerfen.
            drop = i if importance(i) <= importance(i + 1) else i + 1
            if drop == 0 or drop == len(result) - 1:
                drop = i + 1 if i == 0 else i
            if drop == 0 or drop == len(result) - 1:
                continue      # beide sind Randpunkte, nichts zu tun
            del result[drop]
            changed = True
            break

    return result


def create_tracker():
    """Erzeugt einen CSRT-Tracker — hard-fail if CSRT is missing.

    Gekapselt, weil OpenCV die CSRT-Erzeugung je nach opencv-contrib-python-
    Version an verschiedenen Stellen anbietet, und welche davon existiert
    nicht zuverlässig an cv2.__version__ hängt (zwei Nutzer mit demselben
    "pip install opencv-contrib-python" zu unterschiedlichen Zeitpunkten
    können unterschiedliche Stellen haben):

      1. cv2.legacy.TrackerCSRT_create() - Legacy-API, ab ca. 4.5.2
      2. cv2.legacy.TrackerCSRT.create() - Klassenmethode unter legacy
         (OpenCV 5 Docs; zusätzlich zur freien Funktion)
      3. cv2.TrackerCSRT_create() - freie Funktion im Hauptmodul
      4. cv2.TrackerCSRT.create() - klassenbasierte API im Hauptmodul
         (GEMESSEN 16. Sep 2026: reale Install hatte nur diese)

    GEMESSEN (Issue #94/#95, Sep 18 2026, Windows, cv2 5.0.0): manche
    Installationen melden 5.0.0 OHNE jede CSRT-API — typisch wenn
    opencv-python (ohne contrib) opencv-contrib-python überschattet.
    No KCF/MIL soft-fallback: wrong tip quality is worse than a clear
    install error. Product path is Go CSRT (OpenCV builds) or Python CSRT.
    """
    factory = _csrt_factory()
    if factory is not None:
        return factory()
    raise RuntimeError(_opencv_tracker_missing_message())


def _csrt_factory():
    """Callable der einen CSRT erzeugt, oder None."""
    legacy = getattr(cv2, "legacy", None)
    if legacy is not None:
        create_fn = getattr(legacy, "TrackerCSRT_create", None)
        if callable(create_fn):
            return create_fn
        cls = getattr(legacy, "TrackerCSRT", None)
        create_m = getattr(cls, "create", None) if cls is not None else None
        if callable(create_m):
            return create_m
    create_fn = getattr(cv2, "TrackerCSRT_create", None)
    if callable(create_fn):
        return create_fn
    cls = getattr(cv2, "TrackerCSRT", None)
    create_m = getattr(cls, "create", None) if cls is not None else None
    if callable(create_m):
        return create_m
    return None


def _opencv_tracker_missing_message():
    ver = getattr(cv2, "__version__", "?")
    origin = getattr(cv2, "__file__", "?")
    return (
        f"No OpenCV CSRT tracker in cv2 {ver} "
        f"(loaded from {origin}). Common cause: opencv-python without "
        f"contrib shadowing opencv-contrib-python. Fix:\n"
        f"  pip uninstall opencv-python opencv-python-headless\n"
        f"  pip install opencv-contrib-python\n"
        f"Then restart SamNPlayer. Windows portable: use the Go CSRT build "
        f"(uncheck Advanced → Prefer Python) instead of inventing a tip with KCF/MIL."
    )


def opencv_has_usable_tracker():
    """True, wenn create_tracker() einen CSRT erzeugen könnte."""
    return _csrt_factory() is not None


def track_multi_points(video_path, tip_roi, targets, max_frames=None, start_frame=0,
                       mask_rois=None):
    """Track tip + N contact targets; stroke signal = min 2D distance.

    tip_roi: (x,y,w,h) — always tracked (Tf/Tj tip).
    targets: list of (roi, fixed) — contact anchors (ROI2 + --target…).
    Fixed boxes stay at the mark; non-fixed are tracked like the tip.
    mask_rois: reserved soft-exclude boxes (camera/grid punch-outs on other
    paths; distance itself is camera-invariant, so unused here).

    Same return contract as track_two_points / track_roi.
    """
    if not targets:
        raise RuntimeError("track_multi_points requires at least one target")
    # Backward-compatible two-partner path stays available via track_two_points.
    if len(targets) == 1:
        roi_b, fixed_b = targets[0]
        return track_two_points(video_path, tip_roi, roi_b, max_frames=max_frames,
                                start_frame=start_frame, fixed_b=bool(fixed_b))

    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"Could not open video: {video_path}")
    fps = cap.get(cv2.CAP_PROP_FPS) or 25.0
    if start_frame > 0:
        cap.set(cv2.CAP_PROP_POS_FRAMES, start_frame)

    ok, first = cap.read()
    if not ok:
        cap.release()
        raise RuntimeError("Video contains no readable frames")
    height, width = first.shape[:2]

    tip_tracker = create_tracker()
    tip_tracker.init(first, tuple(int(v) for v in tip_roi))
    tip_box = tuple(tip_roi)

    partner_boxes = []
    partner_trackers = []
    for roi, fixed in targets:
        box = tuple(roi)
        partner_boxes.append(box)
        if fixed:
            partner_trackers.append(None)
        else:
            tr = create_tracker()
            tr.init(first, tuple(int(v) for v in box))
            partner_trackers.append(tr)

    center = lambda box: (box[0] + box[2] / 2.0, box[1] + box[3] / 2.0)

    def tip_partner_distance(tip, partner):
        """Distance from tip-box point nearest partner center (not tip center).

        Marking the whole penis still measures contact from the end toward
        the partner (typically glans), so nipple/mouth contact registers.
        """
        px, py = center(partner)
        x, y, w, h = tip
        tx = min(max(px, x), x + w)
        ty = min(max(py, y), y + h)
        return float(np.hypot(px - tx, py - ty))

    def fuse_tip_partners(tip, tip_ok, partners, include):
        """min over included partners; None if tip lost or no usable partner.

        Lost tracked partners must not feed stale boxes into min() (F-006).
        """
        if not tip_ok or not partners:
            return None
        best = None
        for i, p in enumerate(partners):
            if i < len(include) and not include[i]:
                continue
            d = tip_partner_distance(tip, p)
            if best is None or d < best:
                best = d
        return best

    include0 = [True] * len(partner_boxes)
    d0 = fuse_tip_partners(tip_box, True, partner_boxes, include0)
    distances = [d0 if d0 is not None else 0.0]
    timestamps = [0.0]
    lost_flags = [False]
    lost = 0
    idx = 1
    last_dist = distances[0]

    total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)
    if max_frames:
        total = min(total, max_frames) if total else max_frames
    next_report = 0

    while True:
        if max_frames and idx >= max_frames:
            break
        if idx >= next_report:
            print(f"PROGRESS {idx} {total}", file=sys.stderr, flush=True)
            next_report = idx + max(1, (total or 1000) // 100)
        ok, frame = cap.read()
        if not ok:
            break
        ok_tip, new_tip = tip_tracker.update(frame)
        if ok_tip:
            tip_box = new_tip
        include = []
        for i, tr in enumerate(partner_trackers):
            if tr is None:
                include.append(True)  # fixed
                continue
            ok_p, new_p = tr.update(frame)
            if ok_p:
                partner_boxes[i] = new_p
                include.append(True)
            else:
                include.append(False)  # exclude stale
        fused = fuse_tip_partners(tip_box, ok_tip, partner_boxes, include)
        frame_lost = fused is None
        if frame_lost:
            lost += 1
            distances.append(last_dist)
        else:
            last_dist = fused
            distances.append(fused)
        timestamps.append(idx * 1000.0 / fps)
        lost_flags.append(frame_lost)
        idx += 1

    cap.release()
    print(f"PROGRESS {idx} {idx}", file=sys.stderr, flush=True)
    if start_frame > 0:
        offset_ms = int(round(start_frame * 1000 / fps))
        timestamps = [t + offset_ms for t in timestamps]
    distances = np.asarray(distances, dtype=float)
    if lost:
        print(f"Multi-point measurement: in {lost}/{idx} Frames tip lost or no "
              "usable partner (stale boxes excluded from min)", file=sys.stderr)

    stats = {
        "tracker_lost_frames": lost,
        "camera_frames_lost": 0,
        "total_frames": idx,
        "vertical_range": round(float(np.ptp(distances)) if len(distances) else 0.0, 1),
        "horizontal_range": 0.0,
        "tracking_gaps": tracking_gaps_from_flags(timestamps, lost_flags),
        "n_targets": len(targets),
    }
    return np.asarray(timestamps), distances, (width, height), [], stats


def track_two_points(video_path, roi_a, roi_b, max_frames=None, start_frame=0,
                     fixed_b=False):
    """Verfolgt zwei Regionen und liefert ihren ABSTAND (2D) als Signal.

    Der Grund für dieses Verfahren ist mathematisch, nicht heuristisch: ein
    Abstand zwischen zwei Punkten im selben Bild ist von gemeinsamer
    Verschiebung (Kameraschwenk) unabhängig - schwenkt die Kamera, verschieben
    sich BEIDE Punkte gemeinsam, ihr Abstand bleibt. Das Problem entsteht also
    gar nicht erst und muss nicht nachträglich herausgerechnet werden. Das
    gilt NICHT für Zoom: dabei ändert sich der Pixelabstand mit dem
    Zoomfaktor, auch bei unverändertem echtem Abstand. Die ursprüngliche
    Formulierung hier behauptete fälschlich Zoom-Unabhängigkeit
    (docs/FUNGEN_PARITY_PLAN.md, Arbeitspaket 2, hat das zu Recht angemahnt).

    Ebenso fällt gemeinsame Bewegung beider Objekte heraus, die eine
    Einzelpunktmessung fälschlich als Signal sähe.

    Der Abstand wird als vollständiger 2D-Abstand der Boxmittelpunkte
    berechnet, NICHT nur über die Y-Koordinate (siehe two_point_axis_test.py).
    Vorher war es nur der Unterschied der Y-Mittelpunkte - für eine Tf/Tj-
    Konfiguration mit gleicher Höhe für ROI1/ROI2 (etwa aus einer
    automatischen Regionssuche, die ROI2 nur seitlich neben ROI1 setzt, ohne
    Höhenversatz) lieferte das ein Signal nahe Null unabhängig vom
    tatsächlichen Abstand - dieselbe Fehlerklasse, die 2D-Abstand behebt.

    An einem Testvideo mit Kameraschwenk, gemeinsamer Auf-Ab-Bewegung und
    einem schwingenden Abstand gemessen (Korrelation zum echten Abstand):

        ein Tracker    -0.01   (misst die gemeinsame Bewegung, nicht den Abstand)
        zwei Tracker   +0.62

    Die verbleibende Abweichung entsteht, wenn sich die Regionen stark
    überlappen - dann verlieren die Tracker die Zuordnung. Bei deutlicher
    Trennung ist das Verfahren erheblich zuverlässiger.

    Rückgabeform wie track_roi, damit die gesamte nachgelagerte Verarbeitung
    unverändert weiterläuft.
    """
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"Could not open video: {video_path}")
    fps = cap.get(cv2.CAP_PROP_FPS) or 25.0
    if start_frame > 0:
        cap.set(cv2.CAP_PROP_POS_FRAMES, start_frame)

    ok, first = cap.read()
    if not ok:
        cap.release()
        raise RuntimeError("Video contains no readable frames")
    height, width = first.shape[:2]

    tracker_a = create_tracker()
    tracker_a.init(first, tuple(int(v) for v in roi_a))
    tracker_b = None
    if not fixed_b:
        tracker_b = create_tracker()
        tracker_b.init(first, tuple(int(v) for v in roi_b))

    box_a, box_b = tuple(roi_a), tuple(roi_b)
    center = lambda box: (box[0] + box[2] / 2.0, box[1] + box[3] / 2.0)

    def tip_partner_distance(tip, partner):
        """Distance from tip-box point nearest partner center (not tip center)."""
        px, py = center(partner)
        x, y, w, h = tip
        tx = min(max(px, x), x + w)
        ty = min(max(py, y), y + h)
        return float(np.hypot(px - tx, py - ty))

    distances = [tip_partner_distance(box_a, box_b)]
    timestamps = [0.0]
    lost_flags = [False]
    lost = 0
    idx = 1
    last_dist = distances[0]

    total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)
    if max_frames:
        total = min(total, max_frames) if total else max_frames
    next_report = 0

    while True:
        if max_frames and idx >= max_frames:
            break
        if idx >= next_report:
            print(f"PROGRESS {idx} {total}", file=sys.stderr, flush=True)
            next_report = idx + max(1, (total or 1000) // 100)
        ok, frame = cap.read()
        if not ok:
            break
        ok_a, new_a = tracker_a.update(frame)
        if ok_a:
            box_a = new_a
        ok_b = True
        if tracker_b is not None:
            ok_b, new_b = tracker_b.update(frame)
            if ok_b:
                box_b = new_b
        include_b = bool(fixed_b) or ok_b
        if ok_a and include_b:
            last_dist = tip_partner_distance(box_a, box_b)
            distances.append(last_dist)
            frame_lost = False
        else:
            # Tip or tracked partner lost — do not feed stale boxes into signal.
            lost += 1
            distances.append(last_dist)
            frame_lost = True
        timestamps.append(idx * 1000.0 / fps)
        lost_flags.append(frame_lost)
        idx += 1

    cap.release()
    print(f"PROGRESS {idx} {idx}", file=sys.stderr, flush=True)
    if start_frame > 0:
        offset_ms = int(round(start_frame * 1000 / fps))
        timestamps = [t + offset_ms for t in timestamps]
    distances = np.asarray(distances, dtype=float)
    if lost:
        print(f"Zwei-Punkt-Messung: in {lost}/{idx} Frames hat mindestens einer der "
              "beiden Tracker das Ziel verloren", file=sys.stderr)

    stats = {
        "tracker_lost_frames": lost,
        "camera_frames_lost": 0,
        "total_frames": idx,
        "vertical_range": round(float(np.ptp(distances)), 1),
        "horizontal_range": 0.0,
        "tracking_gaps": tracking_gaps_from_flags(timestamps, lost_flags),
    }
    return np.asarray(timestamps), distances, (width, height), [], stats


def tracking_gaps_from_flags(timestamps_ms, lost_flags, merge_gap_ms=100.0):
    """Faßt aufeinanderfolgende Lost-Frames zu [{start_ms, end_ms}, ...] zusammen.

    Kurze Lücken zwischen Lost-Blöcken (< merge_gap_ms) werden mitverschmolzen,
    damit Kontakt-Vibration nicht in jedem Einzel-Frame-Flackern wieder anspringt.
    """
    if not lost_flags or not any(lost_flags):
        return []
    gaps = []
    start = None
    end = None
    for t, lost in zip(timestamps_ms, lost_flags):
        t = float(t)
        if lost:
            if start is None:
                start = end = t
            else:
                end = t
        elif start is not None:
            gaps.append({"start_ms": int(round(start)), "end_ms": int(round(end))})
            start = end = None
    if start is not None:
        gaps.append({"start_ms": int(round(start)), "end_ms": int(round(end))})
    if not gaps:
        return []
    merged = [dict(gaps[0])]
    for g in gaps[1:]:
        prev = merged[-1]
        if g["start_ms"] - prev["end_ms"] <= merge_gap_ms:
            prev["end_ms"] = g["end_ms"]
        else:
            merged.append(dict(g))
    return merged


def _register_builtin_backends():
    """Meldet die eingebauten Verfahren am Register an.

    Sie laufen über denselben Vertrag wie eigene Erweiterungen - sonst wäre
    der Vertrag nur eine Behauptung, und die erste Abweichung fiele erst
    einem fremden Plugin auf.
    """
    import backends

    def csrt(video_path, roi, options):
        return track_roi_cached(
            video_path, roi,
            max_frames=options.get("max_frames"),
            camera_compensation=options.get("camera_compensation", True),
            scene_cut_detection=options.get("scene_cut_detection", True),
            cache_dir=options.get("cache_dir"),
            axis=options.get("axis", "auto"),
            appearance_memory=options.get("appearance_memory", True),
            start_frame=options.get("start_frame", 0),
            mask_rois=options.get("mask_rois"))

    def flow(video_path, roi, options):
        import flow_backend
        downscale = options.get("flow_downscale") or options.get("downscale") or 0
        try:
            downscale = float(downscale)
        except (TypeError, ValueError):
            downscale = 0.0
        # Product default 0.5: full-res Farneback on 720p is often slower
        # than CSRT (~36ms/frame at 0.5× → ~56s for a 50s clip).
        if downscale <= 0:
            downscale = 0.5
        return flow_backend.analyze(
            video_path,
            max_frames=options.get("max_frames"),
            camera_compensation=options.get("camera_compensation", True),
            axis=options.get("axis", "auto"),
            downscale=downscale,
            on_progress=options.get("on_progress"),
        )
    def two_point(video_path, roi, options):
        roi2 = options.get("roi2")
        if not roi2:
            raise RuntimeError("two_point mode requires a second region (--roi2)")
        targets = [(roi2, bool(options.get("roi2_fixed")))]
        for t in options.get("targets") or []:
            # t: (x,y,w,h) or ((x,y,w,h), fixed)
            if isinstance(t, (list, tuple)) and len(t) == 2 and isinstance(t[0], (list, tuple)):
                targets.append((t[0], bool(t[1])))
            else:
                targets.append((t, True))
        return track_multi_points(
            video_path, roi, targets,
            max_frames=options.get("max_frames"),
            start_frame=options.get("start_frame", 0),
            mask_rois=options.get("mask_rois"),
        )

    def grid_lk(video_path, roi, options):
        import grid_lk_backend
        return grid_lk_backend.analyze(video_path, roi, options)

    def region_fusion(video_path, roi, options):
        import region_fusion_backend
        return region_fusion_backend.analyze(video_path, roi, options)

    def region_fusion_auto(video_path, roi, options):
        import region_fusion_auto_backend
        return region_fusion_auto_backend.analyze(video_path, roi, options)

    backends.register("csrt", csrt,
                      "Markierte Region mit einem Tracker verfolgen. Robust bei ruhiger "
                      "Kamera, braucht aber eine Region.")
    backends.register("flow", flow,
                      "Bewegungszentrum je Frame aus dichtem Optical Flow. Keine Region "
                      "nötig. Volle 720p ist oft LANGSAMER als CSRT (~200ms+/Frame gemessen); "
                      "mit --flow-downscale 0.5 brauchbar, Qualität vs FunGen bisher schwach.")
    backends.register("two_point", two_point,
                      "Abstand zweier verfolgter Regionen. Von Kamerabewegung "
                      "mathematisch unabhängig, braucht --roi2.")
    backends.register("grid_lk", grid_lk,
                      "Gitter aus Punkten in der Region, einzeln per Sparse Optical Flow "
                      "verfolgt, Median als Position. Braucht eine Region wie csrt, rund "
                      "15x schneller, GEMESSEN robuster bei kleinen/schwierigen Regionen "
                      "als KCF/MOSSE (siehe docs/NEXT.md Abschnitt 8).")
    backends.register("region_fusion", region_fusion,
                      "Teilt die Region in ein 2x2-Gitter (4 Teilregionen), verfolgt jede "
                      "einzeln und gewichtet sie je Frame nach aktueller Bewegungsstärke "
                      "zu einem Signal - vermeidet, dass ruhige Teile der Region die "
                      "Bewegung in einem aktiven Teil verwässern. Braucht eine Region wie "
                      "csrt/grid_lk.")
    backends.register("region_fusion_auto", region_fusion_auto,
                      "Wie region_fusion, aber ohne markierte Region: teilt automatisch das "
                      "GANZE Bild in ein 2x2-Gitter (4 Zonen) - kein Markier-Schritt nötig, "
                      "wie flow. Arbeitet mit je Zone normalisierten Positionen statt "
                      "absoluten Pixelkoordinaten (die vier Zonen liegen an "
                      "entgegengesetzten Bildecken, anders als bei region_fusion). Noch "
                      "nicht gegen eine Referenz gemessen, siehe docs/NEXT.md.")


def select_roi_interactively(video_path):
    cap = cv2.VideoCapture(video_path)
    ok, frame = cap.read()
    cap.release()
    if not ok:
        raise RuntimeError("Could not read first frame for ROI selection")
    print("Bildregion mit der Maus markieren, dann ENTER/SPACE drücken "
          "(ESC bricht ab).", file=sys.stderr)
    x, y, w, h = cv2.selectROI("ROI wählen - Enter bestätigt", frame, showCrosshair=True)
    cv2.destroyAllWindows()
    if w == 0 or h == 0:
        raise RuntimeError("No ROI selected")
    return (x, y, w, h)


def rdp_simplify(times, values, epsilon):
    """Ramer-Douglas-Peucker-Simplifizierung einer Zeitreihe (siehe
    Projektdokumentation Abschnitt "Adaptive Keyframes"/"RDP").

    Toleranz epsilon ist in denselben Einheiten wie values (hier:
    Positions-Einheiten 0-100), gemessen als vertikaler Abstand eines
    Punkts von der linearen Interpolation zwischen den beiden Randpunkten
    des aktuell betrachteten Abschnitts - nicht als euklidischer Abstand,
    da Zeit (ms) und Position (0-100) stark unterschiedliche Skalen haben
    und ein Vermischen beider in einer Distanz irreführend wäre.

    Gibt eine Boolean-Maske zurück: welche der übergebenen Punkte behalten
    werden. Start- und Endpunkt bleiben immer erhalten.
    """
    n = len(values)
    if n < 3:
        return np.ones(n, dtype=bool)
    keep = np.zeros(n, dtype=bool)
    keep[0] = keep[-1] = True

    def recurse(start, end):
        if end - start < 2:
            return
        t0, t1 = times[start], times[end]
        v0, v1 = values[start], values[end]
        dt = t1 - t0
        max_dist = -1.0
        max_idx = -1
        for i in range(start + 1, end):
            if dt <= 0:
                interp = v0
            else:
                frac = (times[i] - t0) / dt
                interp = v0 + frac * (v1 - v0)
            dist = abs(values[i] - interp)
            if dist > max_dist:
                max_dist = dist
                max_idx = i
        if max_dist > epsilon:
            keep[max_idx] = True
            recurse(start, max_idx)
            recurse(max_idx, end)

    recurse(0, n - 1)
    return keep


def positions_to_funscript(timestamps_ms, y_positions, invert=False,
                            smooth_window=11, min_peak_distance_ms=150,
                            rdp_tolerance=0.0, norm_percentile=2.0,
                            scene_ranges=None, adaptive_error=0.0,
                            min_interval_ms=100.0, dynamic_range_ms=0.0,
                            peak_prominence=0.0, detrend_ms=0.0,
                            bandpass_hz=None):
    """Wandelt die rohe y-Kurve in funscript-Actions um."""
    n = len(y_positions)
    if n < smooth_window:
        smooth_window = n - 1 if n % 2 == 0 else n
    if smooth_window < 5:
        smoothed = y_positions.astype(float)
    else:
        if smooth_window % 2 == 0:
            smooth_window += 1
        smoothed = savgol_filter(y_positions, smooth_window, polyorder=3)

    timestamps_ms = np.asarray(timestamps_ms, dtype=float)
    step = float(np.median(np.diff(timestamps_ms))) if len(timestamps_ms) > 1 else 33.3
    sample_hz = 1000.0 / max(step, 1.0)

    # FunGen/Flow-inspiriert: Drift weg, dann Stroke-Band (0.5–4 Hz typisch).
    if detrend_ms and detrend_ms > 0 and n > 4:
        win = max(3, int(round(detrend_ms / max(step, 1.0))))
        if win % 2 == 0:
            win += 1
        if win > n:
            win = n if n % 2 == 1 else n - 1
        if win >= 3:
            kernel = np.ones(win, dtype=float) / win
            # reflect-pad so edges don't collapse
            pad = win // 2
            padded = np.pad(smoothed, (pad, pad), mode="edge")
            mean = np.convolve(padded, kernel, mode="valid")
            smoothed = smoothed - mean

    if bandpass_hz and n > 8:
        lo, hi = bandpass_hz
        smoothed = _bandpass_1pole(smoothed, sample_hz, lo, hi)

    # Gleitende Dynamik VOR der Normalisierung: schwache Abschnitte auf
    # nutzbare Stärke heben, bevor die Skala festgelegt wird.
    if dynamic_range_ms and dynamic_range_ms > 0 and len(timestamps_ms) > 4:
        window = int(dynamic_range_ms / max(step, 1.0))
        smoothed = dynamic_range_normalize(smoothed, window)

    if scene_ranges:
        smoothed = smoothed.copy()
        for start, end in scene_ranges:
            end = min(end, len(smoothed))
            if end - start < 3:
                continue
            seg = smoothed[start:end]
            if norm_percentile and norm_percentile > 0:
                s_lo = float(np.percentile(seg, norm_percentile))
                s_hi = float(np.percentile(seg, 100.0 - norm_percentile))
            else:
                s_lo, s_hi = float(seg.min()), float(seg.max())
            if s_hi - s_lo < 1e-6:
                s_lo, s_hi = float(seg.min()), float(seg.max())
            if s_hi - s_lo < 1e-6:
                # Szene ohne erkennbare Bewegung: auf die Mitte legen, statt
                # Rauschen auf den vollen Bereich aufzublasen.
                smoothed[start:end] = 0.5
            else:
                smoothed[start:end] = np.clip((seg - s_lo) / (s_hi - s_lo), 0.0, 1.0)
        lo, hi = 0.0, 1.0
    elif norm_percentile and norm_percentile > 0:
        lo = float(np.percentile(smoothed, norm_percentile))
        hi = float(np.percentile(smoothed, 100.0 - norm_percentile))
    else:
        lo, hi = float(smoothed.min()), float(smoothed.max())
    if hi - lo < 1e-6:
        # Perzentile können bei einem fast unbewegten Signal zusammenfallen,
        # obwohl es Ausreißer gibt - dann auf Min/Max zurückfallen, bevor
        # abgebrochen wird.
        lo, hi = float(smoothed.min()), float(smoothed.max())
    if hi - lo < 1e-6:
        raise RuntimeError(
            "No discernible motion in the tracked region — "
            "wrong ROI, or the object is not visibly moving."
        )
    pos = np.clip((smoothed - lo) / (hi - lo), 0.0, 1.0) * 100.0
    if not invert:
        # Bildkoordinaten: kleineres y = weiter oben. funscript-Konvention:
        # 100 = "oben"/maximale Auslenkung. Also invertieren, außer der
        # Nutzer will es andersrum (--invert dreht es nochmal um).
        pos = 100.0 - pos

    fps_estimate = 1000.0 / np.median(np.diff(timestamps_ms)) if n > 1 else 30.0
    min_distance_frames = max(1, int(min_peak_distance_ms / 1000.0 * fps_estimate))

    # Prominenz: wie deutlich hebt sich ein Extremwert von seiner Umgebung
    # ab? Ohne diese Bedingung wird jede kleine Nachschwingung zu einem
    # eigenen Hub.
    #
    # Das ist der Unterschied zwischen zwei Bewegungsarten. Ein starrer Hub
    # ist eine einzelne Bewegung; weiches Gewebe schwingt nach dem Anstoß
    # gedämpft aus, und diese Nachschwingung ist die FOLGE des Anstoßes,
    # kein eigener Hub.
    #
    # Der Preis ist ein höherer Rekonstruktionsfehler - die Nachschwingungen
    # werden bewusst nicht mehr abgebildet. Deshalb Profilentscheidung und
    # keine Voreinstellung.
    peak_kwargs = {"distance": min_distance_frames}
    if peak_prominence and peak_prominence > 0:
        span = float(np.ptp(pos))
        if span > 1e-9:
            peak_kwargs["prominence"] = span * peak_prominence

    peak_idx, _ = find_peaks(pos, **peak_kwargs)
    valley_idx, _ = find_peaks(-pos, **peak_kwargs)
    keyframe_idx = sorted(set([0, n - 1]) | set(peak_idx.tolist()) | set(valley_idx.tolist()))

    # Adaptive Verdichtung: dort zusätzliche Punkte setzen, wo die lineare
    # Interpolation zwischen zwei Extrema die Kurve schlecht wiedergibt.
    # Läuft VOR RDP - RDP räumt danach auf, was zu dicht geraten ist.
    if adaptive_error and adaptive_error > 0:
        keyframe_idx = refine_keyframes(np.asarray(timestamps_ms, dtype=float), pos,
                                        keyframe_idx, max_error=adaptive_error)

    # Mindestabstand ZULETZT durchsetzen, nach Verdichtung und RDP: beide
    # können Punkte hinzufügen bzw. verschieben, eine vorherige Prüfung wäre
    # danach wieder hinfällig.
    if min_interval_ms and min_interval_ms > 0:
        before = len(keyframe_idx)
        keyframe_idx = enforce_min_interval(timestamps_ms, pos, keyframe_idx,
                                            min_interval_ms)
        if len(keyframe_idx) < before:
            print(f"{before - len(keyframe_idx)} Keyframes entfernt, die dichter als "
                  f"{min_interval_ms:.0f}ms lagen (Gerätegrenze)", file=sys.stderr)

    if rdp_tolerance and rdp_tolerance > 0 and len(keyframe_idx) > 2:
        kf_times = np.array([timestamps_ms[i] for i in keyframe_idx], dtype=float)
        kf_vals = np.array([pos[i] for i in keyframe_idx], dtype=float)
        keep_mask = rdp_simplify(kf_times, kf_vals, rdp_tolerance)
        before = len(keyframe_idx)
        keyframe_idx = [idx for idx, keep in zip(keyframe_idx, keep_mask) if keep]
        print(f"RDP-Simplifizierung (Toleranz {rdp_tolerance}): {before} -> {len(keyframe_idx)} Keyframes",
              file=sys.stderr)

    actions = [{"at": int(timestamps_ms[i]), "pos": int(round(pos[i]))} for i in keyframe_idx]
    # Die dichte, normalisierte Kurve wird zusätzlich zurückgegeben, weil der
    # Quality Doctor an den Keyframes allein nicht beurteilen kann, ob echte
    # Bewegung vorlag: die Peak/Valley-Reduktion macht aus JEDEM Signal einen
    # alternierenden Zickzack, der spektral rhythmisch aussieht - auch wenn der
    # Tracker nur zufällig umhergewandert ist. Die Unterscheidung steckt nur
    # noch im ungekürzten Verlauf.
    dense = {"at": np.asarray(timestamps_ms, dtype=float), "pos": pos}
    return actions, dense


def dump_first_frame(video_path, output_png):
    """Speichert den ersten Frame als PNG - für die ROI-Auswahl in der GUI,
    damit der Nutzer sieht, was er markiert, ohne cv2.selectROI (das
    braucht eine lokale X11/GUI-Anzeige, die z.B. bei Fyne-Embedding nicht
    passt)."""
    cap = cv2.VideoCapture(video_path)
    ok, frame = cap.read()
    cap.release()
    if not ok:
        raise RuntimeError("Could not read first frame")
    cv2.imwrite(output_png, frame)
    h, w = frame.shape[:2]
    return w, h


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--video", default=None, help="Pfad zur Videodatei")
    ap.add_argument("--roi", help='ROI als "x,y,w,h" in Pixeln. Ohne diese Option: interaktive Auswahl.')
    ap.add_argument("--output", help="Zielpfad für die .funscript-Datei")
    ap.add_argument("--dump-first-frame", metavar="PNG_PATH",
                     help="Nur den ersten Frame als PNG speichern und beenden (für ROI-Vorschau in der GUI)")
    ap.add_argument("--invert", action="store_true", help="Bewegungsrichtung umkehren")
    ap.add_argument("--smooth-window", type=int, default=11, help="Savitzky-Golay-Fensterbreite (ungerade Zahl)")
    ap.add_argument("--min-peak-distance-ms", type=int, default=150, help="Mindestabstand zwischen erkannten Keyframes in ms")
    ap.add_argument("--max-frames", type=int, default=None, help="Nur die ersten N Frames verarbeiten (zum schnellen Testen)")
    ap.add_argument("--start-seconds", type=float, default=0.0,
                    help="Tracking erst ab dieser Zeit starten (GUI-Seek bei schwarzem Intro)")
    ap.add_argument("--no-camera-compensation", action="store_true",
                     help="Kamerabewegungs-Kompensation abschalten (per Default an - schätzt globale "
                          "Kamerabewegung aus Hintergrund-Features und zieht sie von der Objektkurve ab)")
    ap.add_argument("--no-scene-cut-detection", action="store_true",
                     help="Szenenschnitt-Erkennung abschalten (per Default an - verankert den Tracker bei "
                          "harten Schnitten neu, statt blind über den Schnitt hinweg zu tracken)")
    ap.add_argument("--no-appearance-memory", action="store_true",
                    help="Erscheinungsgedächtnis abschalten. Standardmäßig merkt sich der "
                         "Tracker das Aussehen der Region und sucht sie nach einem Verlust "
                         "oder Szenenschnitt im ganzen Bild wieder, statt blind an der "
                         "letzten Position neu zu verankern.")
    ap.add_argument("--per-scene-roi", action="store_true",
                    help="Nach jedem Szenenschnitt die Bewegungsregion NEU suchen, statt "
                         "die Region der ersten Szene weiterzuverwenden. Deutlich besser bei "
                         "geschnittenem Material, kostet aber einen zusätzlichen Durchlauf.")
    ap.add_argument("--roi-finder", choices=["auto", "ai"], default="auto",
                    help="Verfahren für --per-scene-roi: auto = Rhythmus-Heuristik ohne "
                         "Modell (Standard, siehe auto_roi.py). ai = lokaler ONNX-"
                         "Objekterkenner (siehe ai_roi.py) - braucht onnxruntime und ein "
                         "Modell unter --ai-model-path. Schlägt die Suche für eine Szene "
                         "fehl (Modell fehlt, keine Erkennung), gilt wie bei 'auto' die "
                         "Region der vorherigen Szene weiter - kein Wechsel zwischen den "
                         "Verfahren mitten im Video.")
    ap.add_argument("--ai-model-path", default=None, metavar="DATEI",
                    help="Pfad zur .onnx-Regionsmodell-Datei für --roi-finder ai. Ohne "
                         "Angabe der plattformübliche Modellordner (siehe ai_roi.default_model_path).")
    ap.add_argument("--ai-preferred-classes", default=None, metavar="LISTE",
                    help="Comma-separated class names or ids for --roi-finder ai "
                         "(e.g. hand,breast or 0,1). Names resolve via --ai-classes-json.")
    ap.add_argument("--ai-classes-json", default=None, metavar="PFAD",
                    help="classes.json (or its dataset directory) for name→id "
                         "when using --ai-preferred-classes. Default: next to "
                         "--ai-model-path if present.")
    ap.add_argument("--cache-dir", default=None,
                    help="Verzeichnis für zwischengespeicherte Trackingergebnisse. "
                         "Ohne Angabe wird ein plattformüblicher Ort verwendet.")
    ap.add_argument("--no-cache", action="store_true",
                    help="Trackingergebnisse nicht zwischenspeichern und keinen "
                         "vorhandenen Cache verwenden.")
    ap.add_argument("--norm-percentile", type=float, default=2.0,
                    help="Perzentil für die Normalisierung (Default 2 = robust gegen einzelne "
                         "Tracker-Ausreißer, die sonst die Skala des ganzen Skripts bestimmen). "
                         "0 = altes Min/Max-Verhalten.")
    ap.add_argument("--jobs", type=int, default=1, metavar="N",
                    help="Wie viele Videos im Stapelbetrieb gleichzeitig verarbeitet werden. "
                         "Der größte Geschwindigkeitsgewinn auf einem Mehrkernrechner - die "
                         "Verarbeitung EINES Videos ist weitgehend sequenziell, mehrere sind "
                         "unabhängig. Standard 1.")
    ap.add_argument("--threads", type=int, default=0, metavar="N",
                    help="OpenCV-Threads je Prozess (0 = automatisch).")
    ap.add_argument("--no-opencl", action="store_true",
                    help="Skip automatic OpenCL enable on the Python path (CPU only).")
    ap.add_argument("--hardware-info", action="store_true",
                    help="Verfügbare Beschleunigung anzeigen und beenden.")
    ap.add_argument("--report", default=None, metavar="DATEI",
                    help="Messwerte jedes Laufs als JSON-Zeile anhängen (Kennzahlen, "
                         "Optionen, Laufzeit). Grundlage, um die Qualitätsschwellen an "
                         "echtem Material nachzuziehen statt an synthetischen Testvideos.")
    ap.add_argument("--feedback", nargs="+", metavar=("URTEIL", "KOMMENTAR"),
                    help="Urteil zum zuletzt erzeugten Skript in den Bericht eintragen: "
                         "brauchbar | grenzwertig | unbrauchbar, optional mit Kommentar. "
                         "Braucht --report und --output.")
    ap.add_argument("--train-model", action="store_true",
                    help="Aus den beurteilten Läufen im Bericht ein Qualitätsmodell lernen. "
                         "Wird nur übernommen, wenn es die festen Regeln in einer "
                         "Kreuzvalidierung schlägt. Braucht --report.")
    ap.add_argument("--model-path", default=None, metavar="DATEI",
                    help="Wo das gelernte Modell liegt (Standard: plattformüblicher Ort).")
    ap.add_argument("--no-learned-model", action="store_true",
                    help="Gelerntes Modell ignorieren und nur die festen Regeln anwenden.")
    ap.add_argument("--model-info", action="store_true",
                    help="Das gelernte Modell beschreiben und beenden.")
    ap.add_argument("--label-scene", default=None, metavar="NAME",
                    help="Bewegungssignatur von --video unter NAME speichern (Wiedererkennung "
                         "ähnlicher Szenen, siehe motion_signature.py). Braucht --video, "
                         "verarbeitet das Video sonst nicht, beendet danach.")
    ap.add_argument("--scene-profile", default=None,
                    choices=["standard", "weich", "autotune"],
                    help="Mit --label-scene: das vom Nutzer bestätigte Generatorprofil "
                         "zusammen mit der Signatur speichern. Grundlage für das lokale "
                         "Go-Profilmodell; setzt bei späteren Vorschlägen nichts automatisch.")
    ap.add_argument("--dump-motion-signature", action="store_true",
                    help="Bewegungssignatur von --video als JSON ausgeben und beenden. "
                         "Die Messung bleibt OpenCV-basiert; Training und Inferenz können "
                         "danach im Go-Backend laufen.")
    ap.add_argument("--suggest-profile", action="store_true",
                    help="Bewegungssignatur von --video gegen gespeicherte Szenen (siehe "
                         "--label-scene) vergleichen und ein Profil vorschlagen. Findet sich "
                         "keine ähnliche Szene, wird zusätzlich ein KI-Vorschlag versucht "
                         "(siehe ai_profile.py, --ai-base-url) - beides bleibt ein Vorschlag, "
                         "nichts hiervon setzt --profile automatisch. Braucht --video, "
                         "verarbeitet das Video sonst nicht, beendet danach.")
    ap.add_argument("--signature-path", default=None, metavar="DATEI",
                    help="Wo gespeicherte Szenensignaturen liegen (Standard: "
                         "plattformüblicher Ort, siehe motion_signature.default_labels_path).")
    ap.add_argument("--ai-base-url", default=None, metavar="URL",
                    help="Adresse eines lokal laufenden Colibri-Servers (`coli serve`) für "
                         "--suggest-profile, falls die gespeicherten Szenen keinen sicheren "
                         "Treffer liefern (Standard: http://127.0.0.1:8080). Nicht erreichbar "
                         "-> kein KI-Vorschlag, kein Fehler.")
    ap.add_argument("--ai-quality-opinion", action="store_true",
                    help="Zusätzlich zum Quality Doctor eine Zweitmeinung von einem lokalen "
                         "Colibri-Server einholen (siehe ai_quality.py, --ai-base-url) - "
                         "Fließtext mit Begründung, wird ausgegeben und bei --report mit "
                         "abgelegt. Ändert NICHT quality.passed und trägt kein --feedback "
                         "automatisch nach. Nicht erreichbar -> kein Eintrag, kein Fehler.")
    ap.add_argument("--audio-check", action="store_true",
                    help="Skript-Tempo gegen das Tempo der Audio-Energiehüllkurve des Videos "
                         "prüfen (siehe audio_check.py) - klassische Plausibilitätsprüfung, "
                         "kein KI-Baustein, braucht ffmpeg auf dem PATH. Weicht das Tempo von "
                         "jedem erwarteten Vielfachen (0.5x/1x/2x) ab, wird eine Warnung "
                         "ausgegeben und bei --report mit abgelegt. Ändert NICHT quality.passed "
                         "und korrigiert nichts automatisch. Kein ffmpeg/keine Audiospur -> "
                         "kein Eintrag, kein Fehler.")
    ap.add_argument("--contact-vibration", action="store_true",
                    help="Contact vibration: vibe follows the deep/high-pos slice of this "
                         "script (Tf/Tj distance or stroke/standard/autotune/weich). "
                         "GUI defaults on; CLI opt-in. Without the flag Tf/Tj stays silent.")
    ap.add_argument("--contact-vibration-span", type=float, default=None, metavar="0.4-0.95",
                    help="With --contact-vibration: share of pos spectrum as contact "
                         "(Default 0.75). Lower = earlier; higher = deep only.")
    ap.add_argument("--contact-vibration-curve", default=None,
                    choices=["linear", "soft", "peak", "impulse"],
                    help="With --contact-vibration: envelope linear (Default), "
                         "soft (gentle onset, t²), peak (stronger peak, √t), "
                         "or impulse (quiet mid-window, sharp near deep peaks).")
    ap.add_argument("--report-summary", action="store_true",
                    help="Bericht auswerten und nach Urteil gruppiert ausgeben. "
                         "Braucht --report.")
    ap.add_argument("--script-quality", default=None, metavar="FUNSCRIPT_DATEI",
                    help="Quality Doctor auf eine BEREITS VORHANDENE .funscript-Datei "
                         "anwenden, ohne Video - z.B. eine importierte Datei aus einem "
                         "anderen Werkzeug. Nur die Actions-only-Prüfungen laufen "
                         "(Zeitstempel, Wertebereich, Lücken, Geschwindigkeitsspitzen, "
                         "Keyframe-Dichte, Geräte-Kompatibilität, Rhythmus mit reduzierter "
                         "Verlässlichkeit) - ohne Video fehlen die trackingbasierten "
                         "Prüfungen (aktiver Zeitanteil, Rekonstruktionsfehler, "
                         "Tracker-Verlust, Bewegungsspielraum in Pixeln), das Ergebnis ist "
                         "darum vorsichtiger zu lesen als nach einer echten Generierung.")
    ap.add_argument("--batch", default=None, metavar="ORDNER",
                    help="Alle Videos im Ordner nacheinander verarbeiten. Die Skripte "
                         "werden neben die Videos gelegt. Vorhandene werden übersprungen.")
    ap.add_argument("--plugin-dir", default=None, metavar="ORDNER",
                    help="Verzeichnis mit eigenen Backend-Erweiterungen (Python-Dateien).")
    ap.add_argument("--list-backends", action="store_true",
                    help="Verfügbare Analyseverfahren anzeigen und beenden.")
    ap.add_argument("--backend", default="csrt",
                    help="csrt = markierte Region per Tracker verfolgen (Standard). "
                         "flow = Bewegungszentrum je Frame aus dichtem Optical Flow, "
                         "ohne Tracker/ROI — prefer --flow-downscale 0.5 (full 720p is "
                         "often SLOWER than CSRT; quality vs FunGen still trails). "
                         "grid_lk = Gitter aus Punkten in der Region per Sparse Optical "
                         "Flow verfolgt (Median als Position) - braucht eine Region wie "
                         "csrt, rund 15x schneller, siehe docs/NEXT.md Abschnitt 8. Mit "
                         "--roi2 (Zwei-Punkt-Messung) trackt grid_lk je Region ein "
                         "eigenes Gitter statt csrt's Einzel-Tracker - GEMESSEN "
                         "schlechter für die FunGen-Übereinstimmung als csrt trotz "
                         "besserer eigener Tracking-Güte, siehe docs/NEXT.md Abschnitt 8. "
                         "region_fusion = Region in ein 2x2-Gitter (4 Teilregionen) "
                         "geteilt, jede einzeln getrackt und je Frame nach aktueller "
                         "Bewegungsstärke gewichtet zu einem Signal verschmolzen - "
                         "braucht eine Region wie csrt/grid_lk, kein --roi2. "
                         "region_fusion_auto = wie region_fusion, aber ohne markierte "
                         "Region - teilt automatisch das GANZE Bild in 4 Zonen, wie flow "
                         "also ohne Markier-Schritt, noch nicht gegen eine Referenz "
                         "gemessen.")
    ap.add_argument("--auto-retry", action="store_true",
                    help="Bei nicht bestandener Qualitätsprüfung alternative "
                         "Signalparameter durchprobieren und das beste Ergebnis behalten. "
                         "Billig, weil das Trackingergebnis wiederverwendet wird.")
    ap.add_argument("--max-speed", type=float, default=0.0, metavar="EINHEITEN_PRO_S",
                    help="Positionsänderung auf diesen Wert begrenzen (0-100 Skala pro "
                         "Sekunde). 0 = aus. Sprünge, die schneller sind als das Gerät "
                         "fahren kann, werden nicht schneller ausgeführt, sondern "
                         "abgeschnitten - dann lieber die Amplitude anpassen.")
    ap.add_argument("--detrend-ms", type=float, default=0.0, metavar="MS",
                    help="Gleitendes Mittel (ms) vom Signal abziehen — entfernt "
                         "langsamen Drift (Kamera/Belichtung). 0 = aus. "
                         "Empfehlung im Autotune-Profil: 3000.")
    ap.add_argument("--bandpass-hz", default=None, metavar="LOW,HIGH",
                    help="Stroke-Bandpass in Hz, z.B. 0.5,4 — typisches Geräteband. "
                         "Leer = aus.")
    ap.add_argument("--flow-downscale", type=float, default=0.0, metavar="FAKTOR",
                    help="Optical-Flow-Backend: Frames skalieren. Prefer 0.5 on 720p "
                         "(full-res often slower than CSRT and may not finish). "
                         "0 or 1 = full resolution.")
    ap.add_argument("--roi2", default=None, metavar="x,y,w,h",
                    help="Zweite Region für die Zwei-Punkt-Messung. Das Signal ist dann der "
                         "ABSTAND beider Regionen. Ein Abstand ist von Kamerabewegung "
                         "mathematisch unabhängig - das Problem entsteht gar nicht erst, "
                         "statt nachträglich herausgerechnet zu werden.")
    ap.add_argument("--roi2-fixed", action="store_true",
                    help="Keep ROI2 at the marked box (static contact target); only ROI1 "
                         "is tracked. See docs/BODY_REGIONS.md.")
    ap.add_argument("--target", action="append", default=None, metavar="x,y,w,h[,class]",
                    help="Extra Tf/Tj contact target (repeatable). Distance = min(tip, all "
                         "targets including --roi2). Targets default to fixed. Optional "
                         "5th field = body-part class (docs/BODY_REGIONS.md).")
    ap.add_argument("--mask", action="append", default=None, metavar="x,y,w,h",
                    help="Soft-exclude box (repeatable). Punched out of camera / grid_lk "
                         "feature masks; does not drive the stroke signal.")
    ap.add_argument("--region-class", default=None,
                    help="Optional body-part class for ROI1 (face, mouth, breasts, …).")
    ap.add_argument("--region-class2", default=None,
                    help="Optional body-part class for ROI2.")
    ap.add_argument("--axis", choices=["auto", "y", "x"], default="auto",
                    help="Welche Bewegungsachse ausgewertet wird. Waagerecht und senkrecht "
                         "werden immer BEIDE getrackt (kostet nichts zusätzlich) - auto "
                         "(Standard) wählt danach automatisch die Achse mit der deutlich "
                         "größeren Spannweite. y/x erzwingen stattdessen fest eine Achse, "
                         "falls die automatische Wahl im Einzelfall falsch liegt.")
    ap.add_argument("--profile", choices=["standard", "weich", "tf", "tj", "autotune"],
                    default="standard",
                    help="Voreinstellungen für eine Bewegungsart. 'standard' für Hubbewegung. "
                         "'weich' für weiches Gewebe. 'autotune' = Detrend+Bandpass+Speed-Cap "
                         "(FunGen/Flow-inspirierte Nachbearbeitung, Tracking bleibt klassisch). "
                         "Einzelne Werte lassen sich danach überschreiben.")
    ap.add_argument("--peak-prominence", type=float, default=0.0, metavar="ANTEIL",
                    help="Mindest-Prominenz eines Extremwerts, als Anteil der Gesamtauslenkung. "
                         "0 = aus. Unterdrückt Nachschwingungen. Bis 0.30 bleiben saubere "
                         "Hubsignale unverändert.")
    ap.add_argument("--dynamic-range-ms", type=float, default=0.0, metavar="MS",
                    help="Gleitende Normalisierung über ein Fenster dieser Länge: hebt "
                         "Abschnitte mit schwächerer Bewegung auf nutzbare Stärke, statt "
                         "eine Skala über das ganze Video zu legen. 0 = aus. "
                         "Empfehlung 6000. Die Verstärkung ist gedeckelt, praktisch "
                         "unbewegte Abschnitte werden nicht aufgeblasen.")
    ap.add_argument("--min-action-interval-ms", type=float, default=100.0, metavar="MS",
                    help="Mindestabstand zwischen zwei Actions. Actions, die dichter "
                         "aufeinanderfolgen, kann das Gerät nicht einzeln ausführen - der "
                         "Abschnitt gerät aus dem Takt. 0 = abschalten. Standard 100 "
                         "(Sendeschwelle von Launchcontrol).")
    ap.add_argument("--adaptive-keyframes", type=float, default=0.0, metavar="MAX_ERROR",
                    help="Zusätzliche Keyframes setzen, bis die Kurve auf MAX_ERROR Punkte "
                         "(0-100) genau wiedergegeben wird. 0 = aus. Sinnvoll bei "
                         "asymmetrischen Bewegungen, die reine Hoch-/Tiefpunkte falsch "
                         "beschreiben. Empfehlung: 4-8.")
    ap.add_argument("--rdp-tolerance", type=float, default=0.0,
                     help="Zusätzliche Ramer-Douglas-Peucker-Simplifizierung der Keyframes, Toleranz in "
                          "Positions-Einheiten (0-100). 0 = abgeschaltet (Default).")
    args = ap.parse_args()
    started_at = time.monotonic()

    # Profil anwenden, BEVOR einzelne Werte geprüft werden - ausdrücklich
    # gesetzte Argumente sollen das Profil überschreiben können, nicht
    # umgekehrt.
    if args.profile == "weich":
        defaults = ap.parse_args([])
        if args.peak_prominence == defaults.peak_prominence:
            args.peak_prominence = 0.35
        if args.min_peak_distance_ms == defaults.min_peak_distance_ms:
            args.min_peak_distance_ms = 200
        if args.dynamic_range_ms == defaults.dynamic_range_ms:
            args.dynamic_range_ms = 3000
        print("Profil 'weich': Nachschwingungen werden unterdrückt "
              f"(Prominenz {args.peak_prominence}, Mindestabstand "
              f"{args.min_peak_distance_ms}ms)", file=sys.stderr)

    if args.profile == "autotune":
        defaults = ap.parse_args([])
        if args.detrend_ms == defaults.detrend_ms:
            args.detrend_ms = 3000
        if args.bandpass_hz is None:
            args.bandpass_hz = "0.5,4"
        if args.dynamic_range_ms == defaults.dynamic_range_ms:
            args.dynamic_range_ms = 3000
        if args.peak_prominence == defaults.peak_prominence:
            args.peak_prominence = 0.2
        if args.max_speed == defaults.max_speed:
            args.max_speed = 400
        print("Profil 'autotune': Detrend+Bandpass+Speed-Cap "
              f"(detrend={args.detrend_ms}ms, band={args.bandpass_hz}, "
              f"max_speed={args.max_speed})", file=sys.stderr)

    # Frühe Validierung; die eigentliche Nutzung liegt in process_one.
    try:
        _parse_bandpass_hz(args.bandpass_hz)
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    if is_distance_profile(args.profile):
        if not args.roi2:
            print("Error: tf/tj profile requires --roi2 (second region for distance)", file=sys.stderr)
            sys.exit(1)
        print(f"Profil '{args.profile}': Abstand ROI1–ROI2, Pos geklemmt "
              f"(Sog aus Position)", file=sys.stderr)

    if args.list_backends:
        import backends
        backends.load_plugins(args.plugin_dir or None)
        _register_builtin_backends()
        for name, info in backends.available().items():
            print(f"{name}  [{info['quelle']}]")
            if info["beschreibung"]:
                print(f"    {info['beschreibung']}")
        return

    if args.hardware_info:
        print(describe_hardware())
        return

    configure_threads(args.threads)
    if not args.no_opencl:
        enable_opencl()

    # --- Sonderpfade, die kein Video verarbeiten -------------------------
    if args.model_info:
        import quality_model
        print(quality_model.describe(quality_model.load(args.model_path or None)))
        return

    if args.train_model:
        if not args.report:
            print("Error: --train-model requires --report FILE", file=sys.stderr)
            sys.exit(1)
        import quality_model
        ok, report = quality_model.train(read_report(args.report), args.model_path or None)
        print(report)
        sys.exit(0 if ok else 2)

    if args.report_summary:
        if not args.report:
            print("Error: --report-summary requires --report FILE", file=sys.stderr)
            sys.exit(1)
        print(summarize_report(args.report))
        return

    if args.script_quality:
        import json as _json
        from pathlib import Path as _Path
        import fungen_compare
        import quality_doctor
        actions, failure_reason = fungen_compare.load_actions(_Path(args.script_quality))
        if actions is None:
            print(f"Error: could not read {args.script_quality} "
                  f"({failure_reason})", file=sys.stderr)
            sys.exit(1)
        duration_ms = max(float(a["at"]) for a in actions) - min(float(a["at"]) for a in actions)
        result = quality_doctor.evaluate(actions, video_duration_ms=duration_ms)
        result["estimatedFromScriptOnly"] = True
        print(f"SCRIPT_QUALITY {_json.dumps(result)}")
        return

    if args.feedback:
        if not args.report or not args.output:
            print("Error: --feedback requires --report FILE and --output FILE",
                  file=sys.stderr)
            sys.exit(1)
        verdict = args.feedback[0]
        comment = " ".join(args.feedback[1:])
        try:
            updated = add_feedback(args.report, args.output, verdict, comment)
        except ValueError as exc:
            print(f"Fehler: {exc}", file=sys.stderr)
            sys.exit(1)
        if updated:
            print(f"Urteil '{verdict}' eingetragen.", file=sys.stderr)
        else:
            print("Kein passender Lauf ohne Urteil im Bericht gefunden.", file=sys.stderr)
            sys.exit(1)
        return

    if args.batch:
        run_batch(args, ap)
        return

    process_one(args, ap)


def process_one(args, ap):
    started_at = time.monotonic()
    if not args.video:
        ap.error("--video ist erforderlich (außer bei --report-summary und --feedback)")

    if args.dump_first_frame:
        w, h = dump_first_frame(args.video, args.dump_first_frame)
        print(f"FRAME_SIZE {w} {h}", file=sys.stderr)
        print(f"Geschrieben: {args.dump_first_frame}", file=sys.stderr)
        return

    if args.dump_motion_signature:
        import motion_signature
        signature = motion_signature.extract(args.video)
        print(json.dumps(signature, ensure_ascii=False, sort_keys=True))
        return

    if args.label_scene:
        import motion_signature
        signature = motion_signature.extract(args.video)
        path = args.signature_path or motion_signature.default_labels_path()
        parameters = {"profile": args.scene_profile} if args.scene_profile else None
        motion_signature.save_labelled(path, args.label_scene, signature, parameters)
        print(f"Szene als '{args.label_scene}' gespeichert unter {path}", file=sys.stderr)
        return

    if args.suggest_profile:
        import motion_signature
        signature = motion_signature.extract(args.video)
        path = args.signature_path or motion_signature.default_labels_path()
        known = motion_signature.load_labelled(path)
        match, dist = motion_signature.find_similar(signature, known)
        if match is not None:
            print(f"Vorschlag (gemessen, keine KI): '{match['label']}' "
                  f"(Abstand {dist:.3f})", file=sys.stderr)
            print(f"PROFILE_SUGGESTION {match['label']} measured {dist:.3f}")
            return
        print(f"Keine gespeicherte Szene nah genug (kleinster Abstand {dist:.3f}) - "
              "versuche KI-Vorschlag", file=sys.stderr)
        import ai_profile
        import colibri_client
        base_url = args.ai_base_url or colibri_client.DEFAULT_BASE_URL
        if not ai_profile.available(base_url=base_url):
            print(f"No Colibri server reachable at {base_url} — no suggestion",
                  file=sys.stderr)
            return
        nearest = sorted(known, key=lambda e: motion_signature.distance(
            signature, e.get("signature", {})))[:3]
        result = ai_profile.suggest_profile(signature, known_examples=nearest, base_url=base_url)
        if result is None:
            print("AI returned no usable suggestion", file=sys.stderr)
            return
        print(f"Vorschlag (KI, unverifiziert): '{result['profile']}' "
              f"(Konfidenz {result['confidence']:.2f}) - {result['reason']}", file=sys.stderr)
        print(f"PROFILE_SUGGESTION {result['profile']} ai {result['confidence']:.2f}")
        return

    if not args.output:
        print("Error: --output is required (except with --dump-first-frame)", file=sys.stderr)
        sys.exit(1)

    if args.roi:
        try:
            roi = tuple(int(v) for v in args.roi.split(","))
            if len(roi) != 4:
                raise ValueError
        except ValueError:
            print('Error: --roi must be "x,y,w,h", e.g. "120,80,60,60"', file=sys.stderr)
            sys.exit(1)
    elif args.backend in ("flow", "region_fusion_auto"):
        # Beide Backends bestimmen ihre Zonen/ihr Bewegungszentrum selbst
        # aus dem ganzen Bild - eine markierte Region wäre nicht nur
        # überflüssig, sondern würde den Nutzer zu einer Angabe zwingen,
        # die gar nicht verwendet wird.
        roi = (0, 0, 0, 0)
    else:
        roi = select_roi_interactively(args.video)
        print(f"Gewählte ROI: {roi}", file=sys.stderr)

    print("Tracke ROI durchs Video...", file=sys.stderr)
    cache_dir = None if args.no_cache else (args.cache_dir or default_cache_dir())
    scene_ranges = None
    start_frame = 0
    if getattr(args, "start_seconds", 0) and args.start_seconds > 0:
        # FPS erst nach dem Öffnen bekannt — grobe Schätzung 25, track_*
        # liest die echte FPS und springt über CAP_PROP_POS_FRAMES.
        # Genauer: kurze Probe hier, damit start_frame stimmt.
        _cap = cv2.VideoCapture(args.video)
        _fps = _cap.get(cv2.CAP_PROP_FPS) or 25.0
        _cap.release()
        start_frame = max(0, int(round(args.start_seconds * _fps)))
        if start_frame:
            print(f"Start bei {args.start_seconds:.2f}s (Frame {start_frame})", file=sys.stderr)
    # Soft-exclude masks (repeatable --mask) apply to single-ROI camera /
    # grid_lk feature punch-outs as well as the multi-partner path.
    def _parse_box(flag, s):
        try:
            parts = s.split(",")
            if len(parts) != 4:
                raise ValueError
            return tuple(int(v) for v in parts)
        except ValueError:
            raise SystemExit(f'{flag} must be "x,y,w,h"')

    def _parse_target(s):
        """Parse --target x,y,w,h or x,y,w,h,class → (box, class_or_None)."""
        parts = s.split(",")
        if len(parts) not in (4, 5):
            raise SystemExit('--target must be "x,y,w,h" or "x,y,w,h,class"')
        try:
            box = tuple(int(v) for v in parts[:4])
        except ValueError:
            raise SystemExit('--target must be "x,y,w,h" or "x,y,w,h,class"')
        cls = parts[4].strip() if len(parts) == 5 else None
        if cls == "":
            cls = None
        return box, cls

    mask_rois = [_parse_box("--mask", s) for s in (getattr(args, "mask", None) or [])] or None

    # Stroke profiles (standard/weich/autotune) use tip ROI only for the curve.
    # Keep --roi2 / extras as contact_marks metadata (feel) — do not clear them
    # before stamping. Distance partners still need --profile tf/tj.
    contact_marks_meta = None
    tip_cls = getattr(args, "region_class", None) or None
    primary_cls = getattr(args, "region_class2", None) or None
    stored_roi2 = None
    if args.roi2:
        try:
            stored_roi2 = tuple(int(v) for v in args.roi2.split(","))
            if len(stored_roi2) != 4:
                stored_roi2 = None
        except ValueError:
            stored_roi2 = None
    extras_boxes = []
    for spec in (getattr(args, "target", None) or []):
        try:
            box, cls = _parse_target(spec)
            extras_boxes.append({
                "x": box[0], "y": box[1], "w": box[2], "h": box[3],
                "class": cls or "", "fixed": True,
            })
        except SystemExit:
            raise
        except Exception:
            pass
    tip_box = None
    try:
        if getattr(args, "roi", None):
            tip_t = tuple(int(v) for v in args.roi.split(","))
            if len(tip_t) == 4 and tip_t[2] > 0 and tip_t[3] > 0:
                tip_box = {
                    "x": tip_t[0], "y": tip_t[1],
                    "w": tip_t[2], "h": tip_t[3],
                    "class": tip_cls or "",
                }
    except (ValueError, AttributeError):
        tip_box = None
    if tip_cls or tip_box or stored_roi2 or extras_boxes:
        primary = None
        if stored_roi2:
            primary = {
                "x": stored_roi2[0], "y": stored_roi2[1],
                "w": stored_roi2[2], "h": stored_roi2[3],
                "class": primary_cls or "",
                "fixed": bool(getattr(args, "roi2_fixed", False)),
            }
        contact_marks_meta = {
            "tip_class": tip_cls or "",
            "primary": primary,
            "extras": extras_boxes,
            "drive_stroke": bool(is_distance_profile(args.profile)),
        }
        if tip_box:
            contact_marks_meta["tip"] = tip_box

    if args.roi2 and args.profile not in ("tf", "tj"):
        print("Hint: storing --roi2 as contact mark (feel) — tip CSRT writes the "
              "stroke (use --profile tf or tj for tip↔partner distance).",
              file=sys.stderr)
        args.roi2 = None

    # Verfahren außerhalb der eingebauten Sonderfälle laufen über das
    # Register. Die Sonderfälle bleiben, weil sie zusätzliche Rückgabewerte
    # haben (Szenenbereiche) oder Optionen brauchen, die nicht Teil des
    # Backend-Vertrags sind.
    if args.backend not in ("csrt", "flow") and not args.roi2 and not args.per_scene_roi:
        import backends
        backends.load_plugins(args.plugin_dir or None)
        _register_builtin_backends()
        print(f"Backend '{args.backend}' (über Register)", file=sys.stderr)
        (timestamps_ms, y_positions, frame_size, scene_cuts,
         track_stats) = backends.run(args.backend, args.video, roi, {
            "max_frames": args.max_frames,
            "camera_compensation": not args.no_camera_compensation,
            "scene_cut_detection": not args.no_scene_cut_detection,
            "cache_dir": cache_dir,
            "axis": args.axis,
            "appearance_memory": not args.no_appearance_memory,
            "roi2": None,
            "start_frame": start_frame,
            "flow_downscale": (getattr(args, "flow_downscale", 0) or 0.5),
            "mask_rois": mask_rois,
         })
    elif args.roi2:
        try:
            roi2 = tuple(int(v) for v in args.roi2.split(","))
            if len(roi2) != 4:
                raise ValueError
        except ValueError:
            print('Error: --roi2 must be "x,y,w,h"', file=sys.stderr)
            sys.exit(1)
        print(f"Two-point measurement: {roi} and {roi2}", file=sys.stderr)
        # Zwei-Punkt-Messung hat einen eigenen Dispatch-Zweig (braucht zwei
        # ROIs statt einer und andere Rückgabewerte) und läuft NICHT über
        # das Backend-Register oder track_by_scenes - beide Optionen unten
        # wurden bisher kommentarlos ignoriert, sobald --roi2 gesetzt war.
        # Sichtbar machen statt stillschweigend wirkungslos bleiben (dieselbe
        # Art Fehler wie beim --backend-Fall direkt darunter, siehe #45/#46).
        if args.per_scene_roi:
            print("Hint: --per-scene-roi has no effect with --roi2 (two-point measurement) "
                  "— region is not re-searched after cuts",
                  file=sys.stderr)
        if args.backend not in ("csrt", "grid_lk"):
            print(f"Hint: --backend {args.backend!r} does not support two-point measurement, "
                  "using CSRT (track_two_points) instead", file=sys.stderr)
        extra_targets = []
        for s in (args.target or []):
            box, _cls = _parse_target(s)
            extra_targets.append((box, True))
        if mask_rois is None:
            mask_rois = []

        if args.backend == "grid_lk":
            import grid_lk_backend
            print("Backend 'grid_lk' (multi-point, min distance)", file=sys.stderr)
            timestamps_ms, y_positions, frame_size, scene_cuts, track_stats = (
                grid_lk_backend.analyze_multi_point(
                    args.video, roi, [(roi2, bool(getattr(args, "roi2_fixed", False)))] + extra_targets,
                    {
                        "max_frames": args.max_frames,
                        "scene_cut_detection": not args.no_scene_cut_detection,
                        "mask_rois": mask_rois,
                    }))
        else:
            timestamps_ms, y_positions, frame_size, scene_cuts, track_stats = track_multi_points(
                args.video, roi,
                [(roi2, bool(getattr(args, "roi2_fixed", False)))] + extra_targets,
                max_frames=args.max_frames, start_frame=start_frame,
                mask_rois=mask_rois or None)
    elif args.backend == "flow":
        # Flow: no tracker, no ROI. Historical "~18ms/frame vs CSRT ~100ms" does
        # NOT hold at full 1280×720 (measured ~223ms/frame). Prefer
        # --flow-downscale 0.5; soft FLOW_WALL_WARN / FLOW_STALL_WARN in
        # flow_backend.py. Quality vs FunGen2 still trails CSRT (clip_ausschnitt).
        import flow_backend
        ds = args.flow_downscale if args.flow_downscale > 0 else 0.5
        print(f"Backend: Optical Flow (ohne Tracker/ROI, downscale={ds})",
              file=sys.stderr)
        if args.flow_downscale <= 0:
            print("Hinweis: flow default downscale=0.5 (full-res is often "
                  "slower than CSRT on 720p — set --flow-downscale 1 for full)",
                  file=sys.stderr)
        elif ds >= 1.0:
            print("Hinweis: full-res flow is often slower than CSRT on 720p — "
                  "try --flow-downscale 0.5", file=sys.stderr)
        timestamps_ms, y_positions, frame_size, scene_cuts, track_stats = flow_backend.analyze(
            args.video, max_frames=args.max_frames,
            camera_compensation=not args.no_camera_compensation,
            axis=args.axis,
            downscale=(args.flow_downscale if args.flow_downscale > 0 else 0.5),
            on_progress=lambda done, total: print(f"PROGRESS {done} {total}",
                                                  file=sys.stderr, flush=True))
        if track_stats.get("center_disagreement", 0) > 5:
            print(f"Hinweis: die Bewegungszentrums-Schätzer sind sich uneinig "
                  f"(Streuung {track_stats['center_disagreement']:.1f}px) - das deutet "
                  "auf kein klar dominierendes Bewegungsmuster hin", file=sys.stderr)
    elif args.per_scene_roi:
        roi_finder = None
        if args.roi_finder == "ai":
            import ai_roi

            _ai_pref = None
            if getattr(args, "ai_preferred_classes", None):
                _reg_path = args.ai_classes_json
                if not _reg_path and args.ai_model_path:
                    _reg_path = os.path.dirname(os.path.abspath(args.ai_model_path))
                _reg = ai_roi.load_class_registry(_reg_path) if _reg_path else {}
                _ai_pref = ai_roi.resolve_preferred_class_ids(
                    args.ai_preferred_classes, _reg)

            def roi_finder(path, start, end, _pref=_ai_pref):
                return ai_roi.find_roi(path, start_frame=start, end_frame=end,
                                       report_progress=False, model_path=args.ai_model_path,
                                       preferred_class_ids=_pref)
        (timestamps_ms, y_positions, frame_size, scene_cuts,
         track_stats, scene_ranges) = track_by_scenes(
            args.video, roi, max_frames=args.max_frames,
            camera_compensation=not args.no_camera_compensation,
            roi_finder=roi_finder)
    else:
        timestamps_ms, y_positions, frame_size, scene_cuts, track_stats = track_roi_cached(
            args.video, roi, args.max_frames,
            camera_compensation=not args.no_camera_compensation,
            scene_cut_detection=not args.no_scene_cut_detection,
            cache_dir=cache_dir,
            axis=args.axis,
            appearance_memory=not args.no_appearance_memory,
            start_frame=start_frame,
            mask_rois=mask_rois,
        )
    print(f"{len(timestamps_ms)} Frames getrackt (Videogröße {frame_size[0]}x{frame_size[1]})", file=sys.stderr)

    # Seek (--start-seconds): track_roi returns times relative to start_frame
    # (track_by_scenes chains those itself). For single-pass tracking, shift to
    # absolute video time so the funscript syncs with full-clip playback.
    # track_two_points already applies the offset internally.
    if start_frame > 0 and not args.roi2 and not args.per_scene_roi:
        _cap = cv2.VideoCapture(args.video)
        _fps = _cap.get(cv2.CAP_PROP_FPS) or 25.0
        _cap.release()
        offset_ms = int(round(start_frame * 1000 / _fps))
        timestamps_ms = [float(t) + offset_ms for t in timestamps_ms]
        gaps = track_stats.get("tracking_gaps")
        if gaps:
            track_stats["tracking_gaps"] = [
                {"start_ms": g["start_ms"] + offset_ms, "end_ms": g["end_ms"] + offset_ms}
                if isinstance(g, dict) else g
                for g in gaps
            ]

    # Gelerntes Modell laden, falls vorhanden. Fehlt es, gelten die Regeln.
    try:
        import quality_model
        quality_learned_model = None if args.no_learned_model else quality_model.load(
            args.model_path or None)
    except Exception:
        quality_learned_model = None
    if quality_learned_model:
        print(f"Gelerntes Qualitätsmodell aktiv "
              f"({quality_learned_model['samples']} beurteilte Läufe, "
              f"Trefferquote {quality_learned_model['accuracy']*100:.0f}%)", file=sys.stderr)

    motion_fraction = (track_stats.get("vertical_range", 0.0) / frame_size[1]
                       if frame_size and frame_size[1] else None)
    lost_fraction = track_stats["tracker_lost_frames"] / max(1, track_stats["total_frames"])

    # Bandpass hier parsen (nicht nur in main): process_one ist eine eigene
    # Funktion und sieht Locals aus main nicht — sonst NameError in build().
    try:
        bandpass = _parse_bandpass_hz(getattr(args, "bandpass_hz", None))
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    def build(smooth_window, min_peak_distance_ms, adaptive_error, norm_percentile):
        """Ein kompletter Signalpfad ab der bereits vorhandenen Trackingkurve."""
        acts, dense = positions_to_funscript(
            timestamps_ms, y_positions,
            invert=args.invert,
            smooth_window=smooth_window,
            min_peak_distance_ms=min_peak_distance_ms,
            rdp_tolerance=args.rdp_tolerance,
            norm_percentile=norm_percentile,
            scene_ranges=scene_ranges,
            adaptive_error=adaptive_error,
            min_interval_ms=args.min_action_interval_ms,
            dynamic_range_ms=args.dynamic_range_ms,
            peak_prominence=args.peak_prominence,
            detrend_ms=args.detrend_ms,
            bandpass_hz=bandpass,
        )
        limited = 0
        if args.max_speed > 0:
            acts, limited = limit_speed(acts, args.max_speed)
        result = quality_doctor.evaluate(
            acts,
            video_duration_ms=int(timestamps_ms[-1]),
            dense_signal=dense,
            tracker_lost_fraction=lost_fraction,
            scene_ranges=scene_ranges,
            motion_range_fraction=motion_fraction,
            model=quality_learned_model,
        )
        return acts, dense, result, limited

    base = (args.smooth_window, args.min_peak_distance_ms,
            args.adaptive_keyframes, args.norm_percentile)
    actions, dense_signal, quality, limited_count = build(*base)

    # Automatisches Retry: nur SIGNAL-Parameter werden variiert, das Tracking
    # bleibt unverändert - deshalb kostet ein Versuch Sekundenbruchteile
    # statt Minuten. Probleme, die im Tracking selbst liegen (verlorenes
    # Objekt, kaum Bewegung), lassen sich hier nicht reparieren; dann wird
    # gar nicht erst probiert, statt Zeit zu verbrennen.
    if args.auto_retry and not quality["passed"]:
        untreatable = (lost_fraction > 0.5
                       or (motion_fraction is not None and motion_fraction < 0.03))
        if untreatable:
            print("Auto-retry skipped: problem is in tracking, not signal processing "
                  "— different filter parameters will not help",
                  file=sys.stderr)
        else:
            candidates = [
                (max(5, args.smooth_window - 4), args.min_peak_distance_ms,
                 args.adaptive_keyframes, args.norm_percentile),
                (args.smooth_window + 10, args.min_peak_distance_ms,
                 args.adaptive_keyframes, args.norm_percentile),
                (args.smooth_window + 20, max(250, args.min_peak_distance_ms * 2),
                 args.adaptive_keyframes, args.norm_percentile),
                (args.smooth_window, args.min_peak_distance_ms,
                 args.adaptive_keyframes, 0.0),
            ]
            print(f"Qualitätsprüfung nicht bestanden (Score {quality['score']:.2f}) - "
                  f"probiere {len(candidates)} alternative Signalparameter",
                  file=sys.stderr)
            for cand in candidates:
                try:
                    acts, dense, result, limited = build(*cand)
                except Exception:
                    continue
                if result["score"] > quality["score"]:
                    actions, dense_signal, quality, limited_count = acts, dense, result, limited
                    base = cand
                if quality["passed"]:
                    break
            print(f"Bestes Ergebnis: Score {quality['score']:.2f} "
                  f"(Glättung {base[0]}, Mindestabstand {base[1]}ms, "
                  f"Perzentil {base[3]})", file=sys.stderr)

    if limited_count:
        print(f"{limited_count} Aktion(en) auf {args.max_speed:.0f} Einheiten/s "
              "begrenzt (Gerätegrenze)", file=sys.stderr)

    print(f"{len(actions)} Keyframes erzeugt (aus {len(timestamps_ms)} Frames)", file=sys.stderr)
    print(quality_doctor.format_report(quality), file=sys.stderr)

    ai_opinion = None
    if args.ai_quality_opinion:
        import ai_quality
        import colibri_client
        base_url = args.ai_base_url or colibri_client.DEFAULT_BASE_URL
        if ai_quality.available(base_url=base_url):
            ai_opinion = ai_quality.suggest_quality(
                quality.get("metrics", {}), warnings=quality["warnings"],
                score=quality["score"], rule_passed=quality["passed"], base_url=base_url)
            if ai_opinion is not None:
                print(f"KI-Zweitmeinung: '{ai_opinion['verdict']}' - {ai_opinion['reason']}",
                      file=sys.stderr)
        else:
            print(f"No Colibri server reachable at {base_url} — no second opinion",
                  file=sys.stderr)

    audio_check_result = None
    if args.audio_check:
        import audio_check
        audio_check_result = audio_check.check(args.video, actions)
        if audio_check_result["available"]:
            audio_check.append_quality_audio_hint(bool(quality.get("passed")), audio_check_result)
            print(f"Audio tempo check: script {audio_check_result['script_hz']}, "
                  f"audio {audio_check_result['audio_hz']}", file=sys.stderr)
            for w in audio_check_result["warnings"]:
                print(f"WARNING (audio tempo check): {w}", file=sys.stderr)
        else:
            print(f"Audio tempo check not possible: {audio_check_result['reason']}",
                  file=sys.stderr)

    if is_distance_profile(args.profile):
        actions = clamp_actions_pos(actions)

    metadata = {
        "creator": "SamNPlayer generate_funscript.py (klassisches CV-Tracking, kein Deep Learning)",
        "duration": int(timestamps_ms[-1]),
        "quality_score": quality["score"],
        "quality_passed": quality["passed"],
        "quality_warnings": quality["warnings"],
    }
    # Wie im --report-Eintrag: rein informativ, verändert quality_score/
    # quality_passed nicht und wird nicht automatisch zu einem Feedback-
    # Urteil. Zusätzlich hier (nicht nur im Bericht) hinterlegt, damit die
    # GUI sie direkt aus der erzeugten Datei lesen kann, auch ohne
    # aktivierte Messwert-Aufzeichnung (--report).
    if ai_opinion is not None:
        metadata["ai_opinion"] = ai_opinion
    if audio_check_result is not None and audio_check_result.get("available"):
        metadata["audio_check"] = audio_check_result
    # Tracker-Verlustfenster: Playback schaltet Kontakt-Vibration dort aus
    # (gehaltene Last-Position würde sonst weiterbrummen).
    gaps = track_stats.get("tracking_gaps") or []
    if gaps and (is_distance_profile(args.profile) or args.contact_vibration):
        metadata["tracking_gaps"] = gaps
        print(f"Tracking-Gaps für Kontakt-Vibration: {len(gaps)} Fenster",
              file=sys.stderr)
    metadata = apply_profile_metadata(
        metadata, args.profile,
        contact_vibration=args.contact_vibration,
        contact_vibration_span=args.contact_vibration_span,
        contact_vibration_curve=args.contact_vibration_curve)
    if contact_marks_meta:
        # Drop empty tip_class key for cleaner JSON when unset.
        if not contact_marks_meta.get("tip_class"):
            contact_marks_meta.pop("tip_class", None)
        if not contact_marks_meta.get("extras"):
            contact_marks_meta.pop("extras", None)
        metadata["contact_marks"] = contact_marks_meta

    with open(args.output, "w") as f:
        json.dump({
            "actions": actions,
            "metadata": metadata,
        }, f, indent=2)

    try:
        from samn_write import write_companion_samn
        samn_path = write_companion_samn(
            args.output, actions, metadata, args.profile,
            contact_vibration=args.contact_vibration,
            contact_vibration_span=args.contact_vibration_span,
            contact_vibration_curve=args.contact_vibration_curve)
        if samn_path:
            print(f"Native script written: {samn_path}", file=sys.stderr)
    except Exception as exc:  # noqa: BLE001 — generation must not fail on sidecar
        print(f"Hint: companion .samn not written ({exc})", file=sys.stderr)

    if args.report:
        write_report(args.report, {
            "video": os.path.abspath(args.video),
            "video_name": os.path.basename(args.video),
            "output": os.path.abspath(args.output),
            "generated_at": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
            "engine": {"cache_version": TRACK_CACHE_VERSION,
                       "backend": args.backend},
            "video_info": {"width": frame_size[0], "height": frame_size[1],
                           "duration_ms": int(timestamps_ms[-1]),
                           "frames_analysed": int(track_stats.get("total_frames", 0))},
            "options": {
                "roi": list(roi),
                "camera_compensation": not args.no_camera_compensation,
                "scene_cut_detection": not args.no_scene_cut_detection,
                "per_scene_roi": bool(args.per_scene_roi),
                "axis": args.axis,
                "smooth_window": base[0],
                "min_peak_distance_ms": base[1],
                "adaptive_keyframes": base[2],
                "norm_percentile": base[3],
                "rdp_tolerance": args.rdp_tolerance,
                "max_speed": args.max_speed,
                "auto_retry": bool(args.auto_retry),
            },
            "tracking": track_stats,
            "quality": {"score": quality["score"], "passed": quality["passed"],
                        "warnings": quality["warnings"],
                        "metrics": quality.get("metrics", {}),
                        # Fließtext-Zweitmeinung, nur mit --ai-quality-opinion gefüllt.
                        # Informativ - beeinflusst "passed"/"score" oben nicht und wird
                        # NICHT automatisch zu "feedback" (siehe ai_quality.py).
                        "ai_opinion": ai_opinion,
                        # Nur mit --audio-check gefüllt (siehe audio_check.py) - ebenso
                        # rein informativ, ändert "passed"/"score" oben nicht.
                        "audio_check": audio_check_result},
            "runtime_seconds": round(time.monotonic() - started_at, 1),
            # Platz für dein Urteil. Wird von der GUI bzw. per
            # add_feedback() nachgetragen - siehe --feedback.
            "feedback": None,
        })
        print(f"Messwerte angehängt: {args.report}", file=sys.stderr)

    print(f"Geschrieben: {args.output}", file=sys.stderr)
    if not quality["passed"]:
        print("WARNUNG: Qualitätsprüfung nicht bestanden - Ergebnis vor Gebrauch prüfen (siehe Warnungen oben). "
              "Meist hilft eine engere/genauere ROI-Markierung.", file=sys.stderr)


if __name__ == "__main__":
    main()
