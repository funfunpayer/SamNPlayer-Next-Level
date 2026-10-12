"""Region-Fusion-Backend: die markierte Region in ein 2x2-Gitter (4
Teilregionen) aufteilen, jede einzeln per Sparse Optical Flow verfolgen, und
die vier Signale PRO FRAME nach aktueller Bewegungsstärke gewichtet zu einem
Signal verschmelzen - statt einer einzigen festen Box (csrt) oder eines
einzigen Punktgitters über die ganze Region (grid_lk).

IDEE (Nutzer-Vorschlag, 16. September 2026, nach den Tf/Tj-Messungen in
docs/NEXT.md): bei echtem POV-Material wandert die eigentlich "aktive"
Bewegung oft innerhalb der markierten Region umher (z.B. zwischen oberem und
unterem Bildbereich), eine einzelne Box/ein einzelnes Gitter mittelt das
implizit über die GANZE Region, auch über die Teile, in denen gerade nichts
passiert. Die Region stattdessen in 4 Teilregionen aufzuteilen und die
Fusion dynamisch auf die Teilregion(en) mit der stärksten AKTUELLEN Bewegung
zu gewichten, sollte diese implizite Mittelung über ruhige Bildteile
vermeiden.

WARUM ABSOLUTE POSITIONEN BLENDEN, NICHT NUR EINE REGION AUSWÄHLEN: die vier
Teilregionen liegen alle INNERHALB der vom Nutzer markierten (bereits
lokalisierten) Region, nicht verteilt über das ganze Bild - anders als bei
grob übers ganze Bild verteilten Regionen ist ein gewichteter Mittelwert
ihrer Positionen hier noch aussagekräftig (kein Sprung zwischen weit
entfernten Bildbereichen), und vermeidet die sonst nötigen harten
Umschalt-Sprünge samt Überblend-Logik.

GEMESSEN, nicht nur angenommen: siehe region_fusion_correlation_test.py für
die reale Vergleichsmessung gegen eine FunGen2-Referenz (docs/NEXT.md nennt
die Zahlen). Dieses Verfahren ist ein weiterer Kandidat neben csrt/grid_lk,
kein automatischer Ersatz - --backend wählt weiterhin explizit.

Teilt sich die Bausteine _seed_grid/_reseed mit grid_lk_backend.py (dieselbe
robuste "Median über die überlebenden Punkte, Wiederbesetzung nur lokal per
Maske"-Idee, hier nur pro Teilregion statt für die ganze ROI). Kamerakompen-
sation läuft - anders als bei einem ersten Entwurf dieses Moduls - in
DERSELBEN Dekodierschleife wie das Tracking, nicht als zweiter Durchlauf:
ein Video zweimal zu dekodieren nur für die Kamerakompensation wäre doppelte
Laufzeit für etwas, das track_roi()/grid_lk_backend.py beide inline
erledigen.
"""

import sys

import cv2
import numpy as np
from scipy.signal import savgol_filter

from generate_funscript import detect_scene_cut, estimate_camera_motion
from grid_lk_backend import _reseed, _seed_grid

# 2x2 = 4 Teilregionen, wie vom Nutzer vorgeschlagen. Nicht als CLI-Option
# freigegeben - dieselbe Begründung wie GRID_N/GRID_M in grid_lk_backend.py:
# eine ungeeignete Wahl ist eine unsichtbare Falle, für die es hier (mehr
# Punkte/Regionen) praktisch keinen Geschwindigkeitsgrund gibt, sie dem
# Nutzer zu überlassen.
REGIONS_N = 2
REGIONS_M = 2

# Punkte je Teilregion - kleiner als grid_lk_backend.py's 6x6 (36) für die
# GANZE Region, weil jede Teilregion hier nur ein Viertel der Fläche
# abdeckt; 3x3=9 je Teilregion ergibt in Summe eine vergleichbare
# Gesamtpunktdichte (36) wie grid_lk_backend.py's Einzelgitter.
POINTS_PER_REGION_N = 3
POINTS_PER_REGION_M = 3

# Exponentiell geglättete Bewegungsstärke je Teilregion, Zeitkonstante in
# Frames - bestimmt, wie schnell die Gewichtung einer neu aktiv gewordenen
# Teilregion folgt. Kürzer als das reagiert zu nervös auf einzelne
# Ausreißerframes, länger hinkt der eigentlichen Bewegung spürbar hinterher.
ACTIVITY_EMA_FRAMES = 5
_ACTIVITY_ALPHA = 2.0 / (ACTIVITY_EMA_FRAMES + 1)

DEGRADED_QUORUM_FRACTION = 0.25


def _region_boxes(roi, n, m):
    """Teilt roi=(x,y,w,h) in n*m gleich große Teilregionen, zeilenweise."""
    x, y, w, h = (float(v) for v in roi)
    cw, ch = w / n, h / m
    boxes = []
    for row in range(m):
        for col in range(n):
            boxes.append((x + col * cw, y + row * ch, cw, ch))
    return boxes


def analyze(video_path, roi, options):
    """Verfolgt roi in 4 Teilregionen und verschmilzt sie aktivitäts-
    gewichtet zu einem Signal. Rückgabe: (timestamps_ms, positions,
    (width, height), scene_cuts, stats) - derselbe Vertrag wie backends.py
    ihn verlangt."""
    max_frames = options.get("max_frames")
    camera_compensation = options.get("camera_compensation", True)
    scene_cut_detection = options.get("scene_cut_detection", True)
    axis = options.get("axis", "auto")

    boxes = _region_boxes(roi, REGIONS_N, REGIONS_M)
    n_regions = len(boxes)
    target_n = POINTS_PER_REGION_N * POINTS_PER_REGION_M
    quorum = max(2, int(np.ceil(target_n * DEGRADED_QUORUM_FRACTION)))

    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"Video konnte nicht geöffnet werden: {video_path}")
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

    ok, first_frame = cap.read()
    if not ok:
        cap.release()
        raise RuntimeError("Erster Frame konnte nicht gelesen werden")

    prev_gray = cv2.cvtColor(first_frame, cv2.COLOR_BGR2GRAY)
    pts = [_seed_grid(b, POINTS_PER_REGION_N, POINTS_PER_REGION_M) for b in boxes]
    region_cx = [b[0] + b[2] / 2.0 for b in boxes]
    region_cy = [b[1] + b[3] / 2.0 for b in boxes]
    region_w = [b[2] for b in boxes]
    region_h = [b[3] for b in boxes]
    activity = [0.0] * n_regions
    prev_region_x = list(region_cx)
    prev_region_y = list(region_cy)

    x0, y0, w0, h0 = (float(v) for v in roi)
    timestamps_ms = [0]
    x_positions = [x0 + w0 / 2.0]
    y_positions = [y0 + h0 / 2.0]
    camera_dy_cumulative = [0.0]
    weight_spreads = [0.0]

    tracker_lost_frames = 0  # ALLE Teilregionen ohne überlebenden Punkt
    degraded_frames = 0      # mindestens eine Teilregion unter Quorum
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

        # Bewegungsstärke je Teilregion: Verschiebung ggü. dem vorherigen
        # Frame DERSELBEN Teilregion, exponentiell geglättet - eine still
        # stehende Teilregion verliert so schnell an Gewicht, ein einzelner
        # Ausreißerframe dominiert die Fusion nicht.
        for i in range(n_regions):
            disp = abs(region_x_now[i] - prev_region_x[i]) + abs(region_y_now[i] - prev_region_y[i])
            activity[i] = activity[i] + _ACTIVITY_ALPHA * (disp - activity[i])
        prev_region_x, prev_region_y = list(region_x_now), list(region_y_now)

        total_activity = sum(activity)
        if total_activity > 1e-6:
            weights = [a / total_activity for a in activity]
        else:
            weights = [1.0 / n_regions] * n_regions
        weight_spreads.append(max(weights) - min(weights))

        fused_x = sum(w * v for w, v in zip(weights, region_x_now))
        fused_y = sum(w * v for w, v in zip(weights, region_y_now))

        if camera_compensation:
            if is_cut:
                camera_dy_cumulative.append(0.0)
            else:
                fused_w = sum(w * rw for w, rw in zip(weights, region_w))
                fused_h = sum(w * rh for w, rh in zip(weights, region_h))
                dy = estimate_camera_motion(prev_gray, gray, (fused_x - fused_w / 2.0,
                                                               fused_y - fused_h / 2.0,
                                                               fused_w, fused_h))
                if dy == 0.0:
                    camera_frames_lost += 1
                camera_dy_cumulative.append(camera_dy_cumulative[-1] + dy)
        else:
            camera_dy_cumulative.append(0.0)

        x_positions.append(fused_x)
        y_positions.append(fused_y)

        prev_gray = gray
        timestamps_ms.append(int(frame_idx * 1000 / fps))
        frame_idx += 1
        if max_frames and frame_idx >= max_frames:
            break

    print(f"PROGRESS {frame_idx} {frame_idx}", file=sys.stderr, flush=True)
    cap.release()

    y_positions = np.array(y_positions)
    x_positions = np.array(x_positions)
    camera_dy = np.array(camera_dy_cumulative)
    if camera_compensation:
        segment_bounds = [0] + list(scene_cuts) + [len(camera_dy)]
        for seg_start, seg_end in zip(segment_bounds[:-1], segment_bounds[1:]):
            seg_len = seg_end - seg_start
            if seg_len >= 9:
                camera_dy[seg_start:seg_end] = savgol_filter(
                    camera_dy[seg_start:seg_end], 9, polyorder=2)
        y_positions = y_positions - camera_dy
        if camera_frames_lost > 0:
            print(f"Kamerakompensation: {camera_frames_lost}/{frame_idx} Frames ohne "
                  "verlässliche Hintergrund-Features (unverändert übernommen)", file=sys.stderr)

    if tracker_lost_frames > 0:
        share = tracker_lost_frames / max(1, frame_idx)
        print(f"Region-Fusion: alle Teilregionen haben das Ziel in "
              f"{tracker_lost_frames}/{frame_idx} Frames ({share*100:.0f}%) verloren - dort "
              "wurde die letzte bekannte Position fortgeschrieben", file=sys.stderr)

    vertical_range = float(np.ptp(y_positions)) if len(y_positions) else 0.0
    horizontal_range = float(np.ptp(x_positions)) if len(x_positions) else 0.0

    if axis == "x":
        positions = x_positions
    elif axis == "y":
        positions = y_positions
    else:
        positions = (x_positions if horizontal_range > vertical_range * 1.5
                     and horizontal_range > 5 else y_positions)

    stats = {
        "tracker_lost_frames": tracker_lost_frames,
        "degraded_frames": degraded_frames,
        "camera_frames_lost": camera_frames_lost,
        "total_frames": frame_idx,
        "region_count": n_regions,
        "vertical_range": round(vertical_range, 1),
        "horizontal_range": round(horizontal_range, 1),
        # Wie ungleich die Fusion die vier Teilregionen im Mittel gewichtet
        # hat - nahe 0 heißt praktisch gleichmäßiger Mittelwert (die
        # Aufteilung machte kaum einen Unterschied zu grid_lk), deutlich
        # über 0 heißt die Fusion hat tatsächlich einzelne Teilregionen
        # bevorzugt.
        "mean_weight_spread": round(float(np.mean(weight_spreads)), 3) if weight_spreads else 0.0,
    }
    return np.array(timestamps_ms), positions, (width, height), scene_cuts, stats
