"""Flow-Backend: Bewegungsanalyse ohne Tracker und ohne markierte Region.

Alternative zum CSRT-Weg in generate_funscript.py. Liefert bewusst dieselbe
Rückgabeform wie track_roi(), damit die gesamte nachgelagerte Verarbeitung
(Glättung, Normalisierung, Keyframes, Quality Doctor) unverändert
weiterläuft und beide Wege gegeneinander messbar sind.

Zwei Messungen haben den Entwurf bestimmt:

1. Geschwindigkeit NICHT integrieren. Der naheliegende Weg - mittleren Flow
   pro Frame aufsummieren - liefert unbrauchbare Ergebnisse: an einem Video
   mit 110px echter Bewegung kamen 1.2px heraus, weil sich die
   Schätzfehler gegenseitig aufheben. Stattdessen wird pro Frame eine
   POSITION bestimmt (Schwerpunkt der Bewegungsenergie). Damit entfällt jede
   Integration und damit jede Drift.

2. Nur die stärksten Flow-Anteile zählen. Der Schwerpunkt über das gesamte
   Flow-Feld wird vom Bildzentrum angezogen und lieferte nur 68 von 110px.
   Über die stärksten 5% der Pixel gerechnet: 109.9px. Gegen die bekannte
   Wahrheit gemessen (Korrelationen): Gesamtenergie 0.879, stärkste 20%
   0.898, stärkste 5% 0.904, lokale Varianz 0.918.

Warum überhaupt ein zweites Backend: CSRT kostet rund 100ms pro Frame,
dichter Farneback-Flow 18ms - Faktor 5. Auf eine Stunde Video gerechnet
150 statt 27 Minuten. Und es braucht keine von Hand markierte Region.

Note (21 Sep 2026, lane E): the 18 ms/frame figure is historical and does
not hold as a portable bound for full-resolution 1280×720 analysis. A
bounded synthetic probe on current main averaged ~223 ms/input frame at
scale 1.0 (OpenCV 5 / 8 threads). Progress is now emitted early so a
wall-clock timeout can be distinguished from a true early hang.
"""

import os
import time

import numpy as np

import cv2


# Anteil der stärksten Flow-Pixel, über die der Schwerpunkt gebildet wird.
# 5% ist gemessen, nicht geraten - siehe Modulkommentar.
STRONGEST_FRACTION = 5.0

# Mindestanteil übereinstimmender Punkte, damit eine Kameraschätzung
# übernommen wird. Siehe estimate_camera_shift - an Messwerten festgelegt,
# nicht geraten.
MIN_INLIER_RATIO = 0.6


def _centroid(weights, coords):
    total = float(weights.sum())
    if total <= 1e-6:
        return None
    return float((weights * coords).sum() / total)


def estimate_centers(magnitude, ys, xs, min_motion=0.5):
    """Mehrere unabhängige Schätzer für das Bewegungszentrum.

    Bewusst nur die Verfahren, die sich gegen bekannte Wahrheit bewährt
    haben. Ein Ensemble aus Schätzern, die alle aus demselben Flow-Feld
    schöpfen, mittelt sonst vor allem gemeinsame Fehler.

    GEPRÜFT UND VERWORFEN - Divergenz (d(fx)/dx + d(fy)/dy, das Verfahren von
    Funscript Flow): einzeln gemessen nur 41.1px Spannweite bei 110px echter
    Bewegung und Korrelation 0.829, gegenüber 0.904 (stärkste 5%) und 0.917
    (lokale Varianz). Als dritter Schätzer im Median verschlechterte sie die
    Amplitude auf allen drei Testvideos (clean 104.3->101.0, Schwenk
    105.3->101.2, wenig Kontrast 105.1->102.1) bei praktisch unveränderter
    Korrelation. Der Autor von Funscript Flow berichtet für sein Material das
    Gegenteil - auf unserem trägt es nicht.

    EBENFALLS GEPRÜFT UND VERWORFEN - die symmetrische Projektionsgewichtung
    von Funscript Flow (links/rechts und oben/unten vom Zentrum auf gleiches
    Gesamtgewicht bringen): ohne messbaren Unterschied (110.5 gegen 110.1),
    weil die Gewichtung über die stärksten 5% ohnehin schon auf das Objekt
    konzentriert und dort nichts auszugleichen bleibt.

    min_motion verwirft Frames ohne nennenswerte Bewegung. Das ist nicht
    optional: an den Umkehrpunkten einer Bewegung steht das Objekt kurz
    still, das Flow-Feld enthält dann nur noch Rauschen, und "die stärksten
    5% davon" ist eine beliebige Stelle im Bild. Ohne diese Prüfung springt
    das Zentrum genau dort, wo die Kurve eigentlich ihren sauberen Scheitel
    haben soll. Sichtbar wurde das an einem Video mit rein senkrechter
    Bewegung: die waagerechte Achse hätte konstant bleiben müssen, schwankte
    aber um 121px.

    Die Schwelle wird vom Aufrufer RELATIV zur bisher gesehenen
    Bewegungsstärke gesetzt, nicht als fester Pixelwert. Ein fester Wert ist
    materialabhängig und damit unbrauchbar: auf verrauschtem Hintergrund
    erzeugt schon das Rauschen genug Flow, um jede feste Schwelle zu
    überschreiten, auf sauber texturiertem Hintergrund verwirft derselbe
    Wert die halbe Bewegung.
    """
    if min_motion > 0 and float(np.percentile(magnitude, 95)) < min_motion:
        return None, None, 0.0

    centers_y, centers_x = [], []

    threshold = np.percentile(magnitude, 100.0 - STRONGEST_FRACTION)
    strongest = np.where(magnitude >= threshold, magnitude, 0.0)
    cy = _centroid(strongest, ys)
    cx = _centroid(strongest, xs)
    if cy is not None:
        centers_y.append(cy)
        centers_x.append(cx)

    # Lokale Varianz der Magnitude: betont Bereiche, in denen sich die
    # Bewegung vom Umfeld abhebt, statt gleichmäßig bewegter Flächen.
    variance = cv2.GaussianBlur((magnitude - float(magnitude.mean())) ** 2, (9, 9), 0)
    vy = _centroid(variance, ys)
    vx = _centroid(variance, xs)
    if vy is not None:
        centers_y.append(vy)
        centers_x.append(vx)

    if not centers_y:
        return None, None, 0.0
    # Median statt Mittelwert: ein einzelner ausgerissener Schätzer soll das
    # Ergebnis nicht verschieben.
    spread = float(np.max(centers_y) - np.min(centers_y)) if len(centers_y) > 1 else 0.0
    return float(np.median(centers_y)), float(np.median(centers_x)), spread


def estimate_camera_shift(prev_gray, gray, exclude_center, exclude_radius):
    """Kameraverschiebung (dx, dy) zwischen zwei Frames, aus verfolgten
    Hintergrundmerkmalen.

    Das ersetzt den Median des Flow-Felds, und zwar aus einem gemessenen
    Grund: der Median korrigiert die Flow-VEKTOREN, dieses Backend misst aber
    eine POSITION. Schwenkt die Kamera, wandert das Objekt im Bild mit, und
    ein korrigiertes Vektorfeld ändert daran nichts. Ergebnis vorher: 145.2px
    statt 110px echter Bewegung.

    Hier wird stattdessen dasselbe Verfahren benutzt, das im Tracker-Weg
    nachweislich funktioniert (dort 112.8px): Merkmale AUSSERHALB der
    Bewegungsregion per Lucas-Kanade verfolgen, daraus per RANSAC eine
    affine Transformation schätzen und deren x-/y-Translation als
    Kameraverschiebung nehmen. Diese Verschiebung wird von der gemessenen
    Position abgezogen - Position gegen Position, nicht Vektor gegen Vektor.
    Beide Achsen werden zurückgegeben (nicht nur die bis dahin allein
    genutzte y-Translation), weil analyze() seit der automatischen
    Achsenwahl beide Positionsreihen parallel führt und je nach erkannter
    Bewegungsrichtung die eine oder andere kamerakorrigiert.

    Gibt (0.0, 0.0) zurück, wenn zu wenige verlässliche Punkte gefunden
    wurden. Eine geratene Korrektur wäre schlechter als keine: auf
    strukturlosem Hintergrund liefert die Schätzung Zufallswerte, die sich
    über die Frames zu einem Random Walk aufsummieren würden.
    """
    h, w = prev_gray.shape[:2]
    mask = np.full((h, w), 255, dtype=np.uint8)
    if exclude_center is not None:
        cy, cx = exclude_center
        r = int(exclude_radius)
        y0, y1 = max(0, int(cy) - r), min(h, int(cy) + r)
        x0, x1 = max(0, int(cx) - r), min(w, int(cx) + r)
        mask[y0:y1, x0:x1] = 0

    prev_pts = cv2.goodFeaturesToTrack(
        prev_gray, maxCorners=200, qualityLevel=0.01, minDistance=20,
        blockSize=7, mask=mask)
    if prev_pts is None or len(prev_pts) < 10:
        return 0.0, 0.0, False

    curr_pts, status, _ = cv2.calcOpticalFlowPyrLK(prev_gray, gray, prev_pts, None)
    if curr_pts is None or status is None:
        return 0.0, 0.0, False
    good_prev = prev_pts[status.ravel() == 1]
    good_curr = curr_pts[status.ravel() == 1]
    if len(good_prev) < 10:
        return 0.0, 0.0, False

    matrix, inliers = cv2.estimateAffinePartial2D(
        good_prev, good_curr, method=cv2.RANSAC, ransacReprojThreshold=3.0)
    if matrix is None or inliers is None or int(inliers.sum()) < 8:
        return 0.0, 0.0, False

    # Der ANTEIL übereinstimmender Punkte entscheidet, nicht ihre Anzahl.
    # Auf strukturlosem Hintergrund findet goodFeaturesToTrack reichlich
    # "Ecken" - es ist ja Rauschen -, und einige davon passen zufällig
    # zusammen. Die absolute Zahl täuscht dann eine verlässliche Schätzung
    # vor, und weil die Verschiebungen aufsummiert werden, entsteht daraus
    # ein Random Walk: an einem Rauschvideo gemessen stieg die Amplitude von
    # 105 auf 233px.
    #
    # Der Inlier-Anteil trennt beides sauber: gemessen 0.24 auf
    # Rauschhintergrund gegen 0.84-0.90 auf echter Textur, mit und ohne
    # Kameraschwenk. Die Schwelle liegt mit Abstand dazwischen.
    ratio = float(inliers.sum()) / len(good_prev)
    if ratio < MIN_INLIER_RATIO:
        return 0.0, 0.0, False
    return float(matrix[0, 2]), float(matrix[1, 2]), True


def estimate_global_flow(flow):
    """Globale Kamerabewegung als robuster Median des gesamten Flow-Felds.

    Der Median ist hier angebracht, weil der bewegte Bildteil in aller Regel
    die Minderheit der Pixel ausmacht - die Mehrheit zeigt Hintergrund und
    damit die Kamerabewegung. Ein Mittelwert würde vom bewegten Objekt
    mitgezogen.

    NICHT MEHR FÜR DIE KAMERAKORREKTUR VERWENDET. Das Abziehen korrigiert
    die Flow-VEKTOREN, nicht die gemessene POSITION. Das Backend bestimmt aber
    pro Frame eine Position im Bild. Schwenkt die Kamera, wandert das Objekt
    im Bild mit, und daran ändert ein korrigiertes Vektorfeld nichts - es
    beeinflusst nur, welche Pixel als "stärkste" ausgewählt werden.

    Gemessen an einem realistisch texturierten Schwenkvideo (echte
    Objektbewegung 110px): mit dieser Vektorkorrektur 145.2px, mit der
    merkmalsbasierten Positionskorrektur in estimate_camera_shift 100.2px
    und einer von 0.744 auf 0.805 verbesserten Korrelation. Die Funktion
    bleibt erhalten, weil sie für die Auswahl der stärksten Flow-Anteile
    weiterhin brauchbar ist - für die Kamerakorrektur ist sie es nicht.
    """
    return float(np.median(flow[..., 0])), float(np.median(flow[..., 1]))


def analyze(video_path, max_frames=None, camera_compensation=True,
            downscale=1.0, on_progress=None, axis="auto"):
    """Analysiert das Video und liefert dieselbe Form wie track_roi().

    axis="auto" (Standard) verfolgt waagerechte UND senkrechte Position
    parallel und entscheidet erst am Ende anhand der jeweiligen Spannweite,
    welche Achse die eigentliche Bewegung trägt - siehe track_roi() für die
    Begründung, warum das keine feste Voreinstellung sein soll. "x"/"y"
    erzwingen weiterhin eine Achse.

    Rückgabe: (timestamps_ms, positions, (width, height), scene_cuts, stats)
    """
    timing = os.environ.get("FLOW_BACKEND_TIMING", "").strip() in ("1", "true", "yes")
    # Soft observability only — no default hard kill (owner Flow smoke first).
    # FLOW_BACKEND_WALL_WARN_S: stderr once after this many wall seconds (default 120).
    # FLOW_BACKEND_STALL_WARN_S: stderr if a single Farneback iteration exceeds this (default 60).
    def _env_seconds(name, default):
        raw = os.environ.get(name, "").strip()
        if not raw:
            return default
        try:
            v = float(raw)
        except ValueError:
            return default
        # 0 disables the warning; negative falls back to default.
        if v < 0:
            return default
        return v

    wall_warn_s = _env_seconds("FLOW_BACKEND_WALL_WARN_S", 120.0)
    stall_warn_s = _env_seconds("FLOW_BACKEND_STALL_WARN_S", 60.0)
    wall_t0 = time.monotonic()
    wall_warned = False
    last_frame_t = wall_t0

    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"Video konnte nicht geöffnet werden: {video_path}")

    fps = cap.get(cv2.CAP_PROP_FPS) or 25.0
    total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)
    if max_frames:
        total = min(total, max_frames) if total else max_frames

    ok, first = cap.read()
    if not ok:
        cap.release()
        raise RuntimeError("Video enthält keine lesbaren Frames")

    height, width = first.shape[:2]
    prev = cv2.cvtColor(first, cv2.COLOR_BGR2GRAY)
    if downscale != 1.0:
        prev = cv2.resize(prev, None, fx=downscale, fy=downscale,
                          interpolation=cv2.INTER_AREA)
    h, w = prev.shape
    ys = np.arange(h, dtype=np.float32)[:, None]
    xs = np.arange(w, dtype=np.float32)[None, :]
    scale_back = 1.0 / downscale if downscale != 1.0 else 1.0

    # Laufende Referenz der Bewegungsstärke: der Median der bisher
    # gesehenen 95%-Perzentile. Frames unterhalb eines Bruchteils davon
    # gelten als bewegungslos. Dadurch passt sich die Schwelle an das
    # Material an, statt einen festen Pixelwert zu erzwingen.
    strength_history = []
    # Aufsummierte Kameraverschiebung je Achse. Sie wird von der gemessenen
    # Position abgezogen, damit die Kurve die Bewegung des Objekts
    # beschreibt und nicht die der Kamera. Beide Achsen laufen mit, nicht
    # nur die aktuell gewählte - axis="auto" entscheidet sich erst nach dem
    # kompletten Durchlauf für eine Achse (siehe unten), und dann muss die
    # dazugehörige Kamerakorrektur bereits vorliegen.
    camera_shift_x = 0.0
    camera_shift_y = 0.0
    camera_frames_lost = 0
    last_center = None
    timestamps = [0.0]
    positions_y = [h / 2.0 * scale_back]
    positions_x = [w / 2.0 * scale_back]
    spreads = [0.0]
    no_signal_frames = 0
    idx = 1

    # Early progress so a wall-clock kill can be distinguished from a hang
    # before the first regular report (previously every 10 frames only).
    if on_progress:
        on_progress(1, total)

    while True:
        if max_frames and idx >= max_frames:
            break
        ok, frame = cap.read()
        if not ok:
            break
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        if downscale != 1.0:
            gray = cv2.resize(gray, (w, h), interpolation=cv2.INTER_AREA)

        t0 = time.perf_counter() if timing else None
        flow = cv2.calcOpticalFlowFarneback(prev, gray, None,
                                            0.5, 3, 15, 3, 5, 1.2, 0)
        t_farneback = (time.perf_counter() - t0) * 1000.0 if timing else 0.0

        t0 = time.perf_counter() if timing else None
        if camera_compensation:
            # Merkmalsbasiert und auf der POSITION - siehe
            # estimate_camera_shift zur Begründung.
            shift_x, shift_y, ok_shift = estimate_camera_shift(
                prev, gray, last_center, max(h, w) // 6)
            if ok_shift:
                camera_shift_x += shift_x
                camera_shift_y += shift_y
            else:
                camera_frames_lost += 1
        t_camera = (time.perf_counter() - t0) * 1000.0 if timing else 0.0

        t0 = time.perf_counter() if timing else None
        magnitude = np.linalg.norm(flow, axis=2)
        strength = float(np.percentile(magnitude, 95))
        strength_history.append(strength)
        # Erst ab genügend Vorgeschichte filtern, sonst verwirft der Anfang
        # des Videos sich selbst.
        min_motion = (float(np.median(strength_history)) * 0.25
                      if len(strength_history) >= 25 else 0.0)
        cy, cx, spread = estimate_centers(magnitude, ys, xs, min_motion=min_motion)
        t_centers = (time.perf_counter() - t0) * 1000.0 if timing else 0.0

        if cy is None:
            # Kein verwertbares Zentrum (praktisch bewegungsloser Frame):
            # letzte Position fortschreiben und mitzählen, statt zu raten.
            no_signal_frames += 1
            positions_y.append(positions_y[-1])
            positions_x.append(positions_x[-1])
            spreads.append(spreads[-1])
        else:
            last_center = (cy, cx)
            positions_y.append((cy - camera_shift_y) * scale_back)
            positions_x.append((cx - camera_shift_x) * scale_back)
            spreads.append(spread)

        timestamps.append(idx * 1000.0 / fps)
        prev = gray
        idx += 1

        now = time.monotonic()
        if wall_warn_s > 0 and not wall_warned and (now - wall_t0) >= wall_warn_s:
            wall_warned = True
            import sys
            print(
                f"FLOW_WALL_WARN elapsed={now - wall_t0:.0f}s frame={idx}/{total or '?'} "
                f"size={w}x{h} downscale={downscale} "
                f"(soft warn only — set FLOW_BACKEND_WALL_WARN_S=0 to silence; "
                f"CancelGenerate / Ctrl-C still stops the process)",
                file=sys.stderr, flush=True,
            )
        frame_dt = now - last_frame_t
        if stall_warn_s > 0 and frame_dt >= stall_warn_s:
            import sys
            print(
                f"FLOW_STALL_WARN frame={idx} took {frame_dt:.1f}s "
                f"(Farneback/camera/centers) size={w}x{h} downscale={downscale}",
                file=sys.stderr, flush=True,
            )
        last_frame_t = now

        if timing and (idx <= 5 or idx % 50 == 0):
            import sys
            print(
                f"FLOW_TIMING frame={idx} farneback={t_farneback:.1f}ms "
                f"camera={t_camera:.1f}ms centers={t_centers:.1f}ms "
                f"size={w}x{h} downscale={downscale}",
                file=sys.stderr, flush=True,
            )

        # Report every frame for the first 20, then every 10 — still cheap
        # and makes an early hang visible long before a multi-minute kill.
        if on_progress and (idx <= 20 or idx % 10 == 0):
            on_progress(idx, total)

    cap.release()

    positions_y = np.asarray(positions_y, dtype=float)
    positions_x = np.asarray(positions_x, dtype=float)
    vertical_range = float(np.ptp(positions_y))
    horizontal_range = float(np.ptp(positions_x))
    # Gleiche Schwelle wie track_roi(): erst bei deutlichem, nicht nur
    # geringfügigem Übergewicht der waagerechten Bewegung die Achse
    # wechseln - siehe dort für die Begründung.
    if axis == "x":
        positions = positions_x
    elif axis == "y":
        positions = positions_y
    else:
        positions = (positions_x if horizontal_range > vertical_range * 1.5
                     and horizontal_range > 5 else positions_y)

    stats = {
        # Gleiche Schlüssel wie track_roi, damit der Quality Doctor beide
        # Backends ohne Sonderfall bewerten kann. Der Flow-Weg kann das
        # Objekt nicht im Tracker-Sinn "verlieren"; als Entsprechung zählen
        # Frames ohne verwertbares Bewegungszentrum.
        "tracker_lost_frames": no_signal_frames,
        "camera_frames_lost": camera_frames_lost,
        "total_frames": idx,
        "vertical_range": round(vertical_range, 1),
        "horizontal_range": round(horizontal_range, 1),
        # Streuung zwischen den Schätzern: hoher Wert bedeutet, dass sie sich
        # uneinig sind - ein direktes Vertrauensmaß, das der CSRT-Weg nicht
        # liefern kann.
        "center_disagreement": round(float(np.median(spreads)), 2),
    }
    wall_total = time.monotonic() - wall_t0
    ms_per = (wall_total * 1000.0 / idx) if idx else 0.0
    # Always emit one summary line so slow Flow runs are diagnosable without
    # FLOW_BACKEND_TIMING=1 (owner: "suddenly long — must be traceable").
    import sys
    print(
        f"FLOW_SUMMARY wall={wall_total:.1f}s frames={idx} "
        f"ms_per_frame={ms_per:.1f} size={w}x{h} downscale={downscale} "
        f"farneback_dominates=yes "
        f"(full-res≈4× slower; prefer --flow-downscale 0.5 on 720p+)",
        file=sys.stderr, flush=True,
    )
    return np.asarray(timestamps), positions, (width, height), [], stats
