"""Region-Fusion-Auto-Backend: das GANZE Bild automatisch in ein 2x2-Gitter
(4 Zonen) teilen, jede Zone unabhängig per Punktgitter verfolgen, und die
Zonen aktivitätsgewichtet zu einem Signal verschmelzen - ohne jede manuelle
Regionsmarkierung, genau wie das flow-Backend.

Nutzer-Klarstellung (16. September 2026, nach region_fusion_backend.py):
"ich meinte nicht zwei Stellen markieren, sondern das Bild des Videos
automatisch immer in 4 Zonen teilen und dann mehrere Punkte verteilen" - die
ursprüngliche region_fusion-Idee teilte eine vom Nutzer VON HAND markierte
Region in 4 Teilregionen; das hier ist die tatsächlich gemeinte automatische
Variante ohne Markier-Schritt.

WARUM NICHT EINFACH region_fusion_backend.analyze() MIT roi=GANZES-BILD
AUFRUFEN: region_fusion_backend.py blendet die ABSOLUTEN Pixelpositionen der
vier Teilregionen - dort ausdrücklich damit begründet, dass alle vier
Teilregionen INNERHALB einer bereits lokalisierten, kleinen Region liegen
(siehe dortiger Modulkommentar). Bei vier Zonen, die das GANZE Bild abdecken,
gilt diese Voraussetzung nicht mehr: die Zonen liegen an entgegengesetzten
Bildecken, ein gewichteter Mittelwert ihrer absoluten Pixelkoordinaten wäre
ein bedeutungsloser Sprung zwischen weit entfernten Bildbereichen, sobald die
Gewichtung von einer Zone zur nächsten wechselt.

Stattdessen wird pro Zone die Position INNERHALB der eigenen Zonenbox
normalisiert (0=oberer/linker Rand der Zone, 1=unterer/rechter Rand) - das
ist über alle vier Zonen hinweg vergleichbar, unabhängig davon, WO auf dem
Bild die Zone liegt. Die aktivitätsgewichtete Fusion arbeitet auf diesen
normalisierten Werten, nicht auf rohen Pixelkoordinaten, und wird danach auf
Bildbreite/-höhe zurückskaliert. Geschwindigkeit wird NICHT integriert,
aus demselben gemessenen Grund wie in flow_backend.py - es bleibt bei einer
pro Frame bestimmten POSITION, keiner aufsummierten Geschwindigkeit (das
würde driften).

Kamerakompensation läuft PRO ZONE, nicht einmal fürs ganze Bild: die
bestehende Kamerakompensation (estimate_camera_motion) schätzt aus
Hintergrund-Merkmalen AUSSERHALB der verfolgten Region - bei einer einzigen
Region, die das ganze Bild abdeckt, gäbe es keinen Hintergrund mehr, aus dem
sich schätzen ließe. Pro Zone bleiben die anderen drei Zonen (75% des Bilds)
als Hintergrund übrig, das funktioniert weiterhin. Kostet entsprechend 4x so
viele Kamerakompensations-Schätzungen wie region_fusion/csrt - spürbar
langsamer, nicht nur wegen der vier Punktgitter.

Noch NICHT gegen eine FunGen2-Referenz gemessen (anders als
region_fusion_backend.py, siehe docs/NEXT.md) - ein Kandidat, kein
gemessenes Ergebnis.
"""

import sys

import cv2
import numpy as np
from scipy.signal import savgol_filter

from generate_funscript import detect_scene_cut, estimate_camera_motion
from grid_lk_backend import _reseed, _seed_grid
from region_fusion_backend import _region_boxes

# Dieselbe 2x2-Aufteilung wie region_fusion_backend.py, hier über das ganze
# Bild statt über eine markierte Region - nicht als CLI-Option freigegeben,
# dieselbe Begründung wie dort.
REGIONS_N = 2
REGIONS_M = 2

# GEMESSEN (nicht region_fusion_backend.py's 3x3 übernommen): eine Zone hier
# ist ein Viertel des GANZEN Bildes, oft hunderte Pixel groß - ein 3x3-Gitter
# (wie region_fusion_backend.py es für eine kleine, bereits eng markierte
# Region benutzt) lässt dort so große Lücken zwischen den Punkten, dass ein
# kleineres bewegtes Objekt zwischen alle Gitterpunkte fällt und komplett
# unentdeckt bleibt (an einem 320x240-Testvideo mit einer 160x120-Zone: ein
# 15px-Objekt lieferte praktisch 0px Ausschlag statt der echten 30px). 6x6,
# dieselbe Dichte wie grid_lk_backend.py's GRID_N/GRID_M für eine einzelne
# ganze ROI, behebt das im selben Test.
POINTS_PER_REGION_N = 6
POINTS_PER_REGION_M = 6
ACTIVITY_EMA_FRAMES = 5
_ACTIVITY_ALPHA = 2.0 / (ACTIVITY_EMA_FRAMES + 1)
DEGRADED_QUORUM_FRACTION = 0.25


def analyze(video_path, roi, options):
    """roi wird ignoriert (wie beim flow-Backend) - dieses Verfahren braucht
    keine markierte Region, es teilt immer automatisch das ganze Bild.
    Rückgabe: (timestamps_ms, positions, (width, height), scene_cuts,
    stats), derselbe Vertrag wie backends.py ihn verlangt."""
    max_frames = options.get("max_frames")
    camera_compensation = options.get("camera_compensation", True)
    scene_cut_detection = options.get("scene_cut_detection", True)
    axis = options.get("axis", "auto")

    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"Video konnte nicht geöffnet werden: {video_path}")
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

    boxes = _region_boxes((0, 0, width, height), REGIONS_N, REGIONS_M)
    n_regions = len(boxes)
    target_n = POINTS_PER_REGION_N * POINTS_PER_REGION_M
    quorum = max(2, int(np.ceil(target_n * DEGRADED_QUORUM_FRACTION)))

    ok, first_frame = cap.read()
    if not ok:
        cap.release()
        raise RuntimeError("Erster Frame konnte nicht gelesen werden")

    prev_gray = cv2.cvtColor(first_frame, cv2.COLOR_BGR2GRAY)
    pts = [_seed_grid(b, POINTS_PER_REGION_N, POINTS_PER_REGION_M) for b in boxes]
    region_cx = [b[0] + b[2] / 2.0 for b in boxes]
    region_cy = [b[1] + b[3] / 2.0 for b in boxes]
    region_x0 = [b[0] for b in boxes]
    region_y0 = [b[1] for b in boxes]
    region_w = [b[2] for b in boxes]
    region_h = [b[3] for b in boxes]
    activity = [0.0] * n_regions
    prev_region_x = list(region_cx)
    prev_region_y = list(region_cy)

    timestamps_ms = [0]
    # Rohe (noch nicht kamerakorrigierte) Positionen je Zone - erst nach der
    # Schleife segmentweise geglättet, wie in region_fusion_backend.py.
    y_by_region = [[region_cy[i]] for i in range(n_regions)]
    x_by_region = [[region_cx[i]] for i in range(n_regions)]
    weights_history = [[1.0 / n_regions] * n_regions]
    camera_dy_cumulative = [[0.0] for _ in range(n_regions)]

    tracker_lost_frames = 0
    degraded_frames = 0
    camera_frames_lost = 0
    scene_cuts = []

    frame_idx = 1
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
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

        is_cut = scene_cut_detection and detect_scene_cut(prev_gray, gray)
        if is_cut:
            scene_cuts.append(frame_idx)

        region_x_now = [0.0] * n_regions
        region_y_now = [0.0] * n_regions
        any_survivor = False
        degraded_this_frame = False

        for i in range(n_regions):
            region_pts = pts[i]
            if is_cut or region_pts is None or len(region_pts) == 0:
                fresh = _reseed(gray, region_cx[i], region_cy[i], region_w[i], region_h[i], target_n)
                pts[i] = fresh[:target_n] if fresh is not None and len(fresh) > 0 else None
                survivors_xy = None
            else:
                new_pts, status, _err = cv2.calcOpticalFlowPyrLK(prev_gray, gray, region_pts, None)
                if new_pts is None or status is None:
                    survivors_xy, pts[i] = None, None
                else:
                    status = status.reshape(-1)
                    survivors_xy = new_pts.reshape(-1, 2)[status == 1]

            n_survivors = 0 if survivors_xy is None else len(survivors_xy)
            if n_survivors == 0:
                region_x_now[i] = region_cx[i]
                region_y_now[i] = region_cy[i]
                if pts[i] is None or len(pts[i]) == 0:
                    fresh = _reseed(gray, region_cx[i], region_cy[i], region_w[i], region_h[i], target_n)
                    pts[i] = fresh[:target_n] if fresh is not None and len(fresh) > 0 else None
            else:
                any_survivor = True
                if n_survivors < quorum:
                    degraded_this_frame = True
                med_x = float(np.median(survivors_xy[:, 0]))
                med_y = float(np.median(survivors_xy[:, 1]))
                region_cx[i], region_cy[i] = med_x, med_y
                region_x_now[i], region_y_now[i] = med_x, med_y

                missing = target_n - n_survivors
                if missing > 0:
                    fresh = _reseed(gray, med_x, med_y, region_w[i], region_h[i], missing)
                    if fresh is not None and len(fresh) > 0:
                        take = fresh[:missing].reshape(-1, 1, 2).astype(np.float32)
                        pts[i] = np.concatenate(
                            [survivors_xy.reshape(-1, 1, 2).astype(np.float32), take], axis=0)
                    else:
                        pts[i] = survivors_xy.reshape(-1, 1, 2).astype(np.float32)
                else:
                    pts[i] = survivors_xy[:target_n].reshape(-1, 1, 2).astype(np.float32)

        if not any_survivor:
            tracker_lost_frames += 1
        elif degraded_this_frame:
            degraded_frames += 1

        for i in range(n_regions):
            disp = abs(region_x_now[i] - prev_region_x[i]) + abs(region_y_now[i] - prev_region_y[i])
            activity[i] = activity[i] + _ACTIVITY_ALPHA * (disp - activity[i])
        prev_region_x, prev_region_y = list(region_x_now), list(region_y_now)

        total_activity = sum(activity)
        if total_activity > 1e-6:
            weights = [a / total_activity for a in activity]
        else:
            weights = [1.0 / n_regions] * n_regions
        weights_history.append(weights)

        frame_camera_failed = False
        for i in range(n_regions):
            x_by_region[i].append(region_x_now[i])
            y_by_region[i].append(region_y_now[i])
            if camera_compensation:
                if is_cut:
                    camera_dy_cumulative[i].append(0.0)
                else:
                    dy = estimate_camera_motion(prev_gray, gray, boxes[i])
                    if dy == 0.0:
                        frame_camera_failed = True
                    camera_dy_cumulative[i].append(camera_dy_cumulative[i][-1] + dy)
            else:
                camera_dy_cumulative[i].append(0.0)
        if camera_compensation and not is_cut and frame_camera_failed:
            camera_frames_lost += 1

        prev_gray = gray
        timestamps_ms.append(int(frame_idx * 1000 / fps))
        frame_idx += 1
        if max_frames and frame_idx >= max_frames:
            break

    print(f"PROGRESS {frame_idx} {frame_idx}", file=sys.stderr, flush=True)
    cap.release()

    y_by_region = [np.array(a, dtype=float) for a in y_by_region]
    x_by_region = [np.array(a, dtype=float) for a in x_by_region]
    if camera_compensation:
        segment_bounds = [0] + list(scene_cuts) + [len(y_by_region[0])]
        for i in range(n_regions):
            camera_dy = np.array(camera_dy_cumulative[i])
            for seg_start, seg_end in zip(segment_bounds[:-1], segment_bounds[1:]):
                seg_len = seg_end - seg_start
                if seg_len >= 9:
                    camera_dy[seg_start:seg_end] = savgol_filter(
                        camera_dy[seg_start:seg_end], 9, polyorder=2)
            y_by_region[i] = y_by_region[i] - camera_dy
        if camera_frames_lost > 0:
            print(f"Kamerakompensation: in {camera_frames_lost}/{frame_idx} Frames hatte "
                  "mindestens eine Zone keine verlässlichen Hintergrund-Features (dort "
                  "unverändert übernommen)", file=sys.stderr)

    if tracker_lost_frames > 0:
        share = tracker_lost_frames / max(1, frame_idx)
        print(f"Region-Fusion-Auto: alle Zonen haben das Ziel in "
              f"{tracker_lost_frames}/{frame_idx} Frames ({share*100:.0f}%) verloren - dort "
              "wurde die letzte bekannte Position fortgeschrieben", file=sys.stderr)

    # Je Zone auf die eigene Box normalisieren (0..1) - siehe Modulkommentar,
    # warum absolute Pixelkoordinaten hier NICHT direkt geblendet werden
    # dürfen (die vier Zonen liegen an entgegengesetzten Bildecken).
    n_frames = len(timestamps_ms)
    norm_y = np.zeros((n_regions, n_frames))
    norm_x = np.zeros((n_regions, n_frames))
    for i in range(n_regions):
        norm_y[i] = np.clip((y_by_region[i] - region_y0[i]) / max(1.0, region_h[i]), 0.0, 1.0)
        norm_x[i] = np.clip((x_by_region[i] - region_x0[i]) / max(1.0, region_w[i]), 0.0, 1.0)

    weights_arr = np.array(weights_history).T  # (n_regions, n_frames)
    fused_norm_y = np.sum(weights_arr * norm_y, axis=0)
    fused_norm_x = np.sum(weights_arr * norm_x, axis=0)

    # Zurück auf eine pixelähnliche Skala (Bildhöhe/-breite), damit
    # nachgelagerte Statistiken (vertical_range etc.) Größenordnungen liefern,
    # die mit den anderen Backends vergleichbar sind.
    y_positions = fused_norm_y * height
    x_positions = fused_norm_x * width

    vertical_range = float(np.ptp(y_positions)) if len(y_positions) else 0.0
    horizontal_range = float(np.ptp(x_positions)) if len(x_positions) else 0.0

    if axis == "x":
        positions = x_positions
    elif axis == "y":
        positions = y_positions
    else:
        positions = (x_positions if horizontal_range > vertical_range * 1.5
                     and horizontal_range > 5 else y_positions)

    weight_spread = (np.max(weights_arr, axis=0) - np.min(weights_arr, axis=0)
                      if weights_arr.size else np.array([0.0]))

    stats = {
        "tracker_lost_frames": tracker_lost_frames,
        "degraded_frames": degraded_frames,
        "camera_frames_lost": camera_frames_lost,
        "total_frames": frame_idx,
        "region_count": n_regions,
        "vertical_range": round(vertical_range, 1),
        "horizontal_range": round(horizontal_range, 1),
        "mean_weight_spread": round(float(np.mean(weight_spread)), 3),
    }
    return np.array(timestamps_ms), positions, (width, height), scene_cuts, stats
