"""Regressionstest für das Region-Fusion-Backend.

Prüft dieselben Grundeigenschaften wie grid_lk_backend_test.py (Vertragsform,
Amplitude, Kamerakompensation, Achsenwahl, max_frames) UND die eigentliche
Kernhypothese dieses Backends (siehe Modulkommentar in
region_fusion_backend.py): wenn die Bewegung nur in EINER der vier
Teilregionen stattfindet (die anderen drei bleiben ruhig), soll die
aktivitätsgewichtete Fusion die Amplitude trotzdem weitgehend zeigen - eine
naive Gleichgewichtung über alle vier Teilregionen würde sie dagegen auf
etwa ein Viertel verwässern (3 von 4 Teilregionen liefern nahe-Null-Signal).

Dauer: rund zwei bis drei Minuten (vier Teilregionen statt einer, also
grob 4x mehr Tracking-Aufwand als grid_lk_backend_test.py pro Frame).
Ausführen: python3 generator/region_fusion_backend_test.py
"""

import sys
import tempfile
from pathlib import Path

import cv2
import numpy as np

import region_fusion_backend

W, H, FPS = 320, 240, 25
TRUE_AMPLITUDE = 80.0
ROI = (110, 60, 100, 100)


def textured_background(seed=11):
    rng = np.random.default_rng(seed)
    bg = np.full((H, W, 3), 40, np.uint8)
    for _ in range(140):
        x, y = int(rng.integers(0, W)), int(rng.integers(0, H))
        color = tuple(int(v) for v in rng.integers(60, 190, 3))
        cv2.rectangle(bg, (x, y), (x + int(rng.integers(6, 18)), y + int(rng.integers(6, 18))),
                      color, -1)
    return cv2.GaussianBlur(bg, (3, 3), 0)


def write_moving(path, seconds=8, freq=1.0):
    bg = textured_background()
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    for i in range(FPS * seconds):
        t = i / FPS
        frame = bg.copy()
        y = int(160 + (TRUE_AMPLITUDE / 2) * np.sin(2 * np.pi * freq * t))
        cv2.circle(frame, (160, y), 35, (250, 250, 250), -1)
        vw.write(frame)
    vw.release()


def write_moving_one_quadrant(path, seconds=8, freq=1.0):
    """Bewegtes Objekt NUR innerhalb der oberen linken Teilregion der ROI
    (x=110..160, y=60..110) - die anderen drei Teilregionen zeigen
    ausschließlich den texturierten, unbewegten Hintergrund. Kleinerer
    Kreis (18px) und geringere Amplitude als write_moving(), damit die
    Bewegung tatsächlich innerhalb der 50x50-Teilregion bleibt."""
    bg = textured_background()
    amplitude = 25.0
    cx = 135  # Mitte der oberen linken Teilregion (110 + 50/2)
    cy_base = 85
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    for i in range(FPS * seconds):
        t = i / FPS
        frame = bg.copy()
        y = int(cy_base + (amplitude / 2) * np.sin(2 * np.pi * freq * t))
        cv2.circle(frame, (cx, y), 15, (250, 250, 250), -1)
        vw.write(frame)
    vw.release()
    return amplitude


def write_moving_with_pan(path, seconds=10, freq=1.0):
    bg = textured_background()
    big = cv2.copyMakeBorder(bg, 60, 60, 60, 60, cv2.BORDER_REFLECT)
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    for i in range(FPS * seconds):
        t = i / FPS
        off_x = int(60 + 30 * np.sin(2 * np.pi * t * 0.11))
        off_y = int(60 + 20 * np.sin(2 * np.pi * t * 0.08))
        frame = big[off_y:off_y + H, off_x:off_x + W].copy()
        world_y = 60 + 160 + (TRUE_AMPLITUDE / 2) * np.sin(2 * np.pi * freq * t)
        cv2.circle(frame, (160, int(world_y - off_y)), 35, (250, 250, 250), -1)
        vw.write(frame)
    vw.release()


def write_with_hard_cut(path, seconds=8, cut_at=100):
    bg1 = textured_background(seed=11)
    bg2 = textured_background(seed=99)
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    n = FPS * seconds
    for i in range(n):
        t = i / FPS
        bg = bg1 if i < cut_at else bg2
        frame = bg.copy()
        if i >= cut_at:
            frame[:, :] = cv2.addWeighted(frame, 0.4, np.full_like(frame, 20), 0.6, 0)
        y = int(160 + (TRUE_AMPLITUDE / 2) * np.sin(2 * np.pi * t))
        cv2.circle(frame, (160, y), 35, (250, 250, 250), -1)
        vw.write(frame)
    vw.release()


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)

        moving = tmp / "moving.mp4"
        write_moving(moving, seconds=8)
        ts, pos, size, cuts, stats = region_fusion_backend.analyze(
            str(moving), ROI, {"camera_compensation": False, "scene_cut_detection": False})

        check("liefert dieselbe Rückgabeform wie track_roi/grid_lk_backend",
              len(ts) == len(pos) and size == (W, H) and isinstance(stats, dict))
        for key in ("tracker_lost_frames", "camera_frames_lost", "total_frames",
                   "vertical_range", "horizontal_range"):
            check(f"stats enthält Pflichtfeld {key!r}", key in stats)
        check("Zeitstempel streng aufsteigend", bool(np.all(np.diff(ts) > 0)))
        check("Amplitude trifft die echte Bewegung ungefähr (Bewegung über die ganze ROI)",
              abs(np.ptp(pos) - TRUE_AMPLITUDE) < 25,
              f"{np.ptp(pos):.1f} statt {TRUE_AMPLITUDE}")
        check("praktisch keine komplett verlorenen Frames auf einfachem Material",
              stats["tracker_lost_frames"] / stats["total_frames"] < 0.05,
              str(stats["tracker_lost_frames"]))
        check("4 Teilregionen gemeldet", stats["region_count"] == 4, str(stats["region_count"]))

        # --- Kernhypothese: Bewegung in nur EINER Teilregion wird nicht
        # durch die drei ruhigen Teilregionen verwässert -------------------
        one_quad = tmp / "one_quadrant.mp4"
        quad_amplitude = write_moving_one_quadrant(one_quad, seconds=8)
        _, pos_quad, _, _, stats_quad = region_fusion_backend.analyze(
            str(one_quad), ROI, {"camera_compensation": False, "scene_cut_detection": False})
        naive_average_amplitude = quad_amplitude / 4  # Gleichgewichtung würde auf das verwässern
        check("Fusion zeigt deutlich mehr Amplitude als eine naive Gleichgewichtung "
              "über alle vier Teilregionen liefern würde",
              float(np.ptp(pos_quad)) > naive_average_amplitude * 2,
              f"{np.ptp(pos_quad):.1f} vs naiv {naive_average_amplitude:.1f}")
        check("Fusion gewichtet die Teilregionen tatsächlich ungleich "
              "(mean_weight_spread deutlich über 0)",
              stats_quad["mean_weight_spread"] > 0.15,
              str(stats_quad["mean_weight_spread"]))

        # --- Szenenschnitt: erkannt, Gitter erholt sich danach ---------------
        cut_video = tmp / "cut.mp4"
        write_with_hard_cut(cut_video, seconds=8, cut_at=100)
        _, pos_cut, _, cuts_cut, stats_cut = region_fusion_backend.analyze(
            str(cut_video), ROI, {"camera_compensation": False, "scene_cut_detection": True})
        check("Szenenschnitt wird erkannt", len(cuts_cut) >= 1, str(cuts_cut))
        check("Fusion erholt sich nach dem Schnitt (Bewegung weiterhin sichtbar)",
              np.ptp(pos_cut[110:]) > TRUE_AMPLITUDE * 0.3,
              f"{np.ptp(pos_cut[110:]):.1f}")

        # --- Kamerakompensation ------------------------------------------------
        panning = tmp / "panning.mp4"
        write_moving_with_pan(panning, seconds=10)
        _, pos_pan, _, _, _ = region_fusion_backend.analyze(
            str(panning), ROI, {"camera_compensation": True, "scene_cut_detection": False})
        overshoot = float(np.ptp(pos_pan))
        check("Amplitude bei Schwenk in vertretbarem Rahmen (mit Kompensation)",
              overshoot < TRUE_AMPLITUDE * 1.8,
              f"{overshoot:.1f} statt {TRUE_AMPLITUDE}")

        _, pos_pan_uncorrected, _, _, _ = region_fusion_backend.analyze(
            str(panning), ROI, {"camera_compensation": False, "scene_cut_detection": False})
        check("Kamerakorrektur verbessert die Amplitude messbar",
              abs(np.ptp(pos_pan_uncorrected) - TRUE_AMPLITUDE) > abs(overshoot - TRUE_AMPLITUDE),
              f"ohne {np.ptp(pos_pan_uncorrected):.1f}, mit {overshoot:.1f}")

        # --- Achsenwahl ------------------------------------------------------
        _, pos_x, _, _, _ = region_fusion_backend.analyze(
            str(moving), ROI, {"camera_compensation": False, "scene_cut_detection": False,
                               "axis": "x"})
        check("waagerechte Achse liefert bei senkrechter Bewegung wenig Ausschlag",
              float(np.ptp(pos_x)) < float(np.ptp(pos)),
              f"x {np.ptp(pos_x):.1f} vs y {np.ptp(pos):.1f}")

        # --- max_frames wird respektiert -------------------------------------
        _, pos_short, _, _, stats_short = region_fusion_backend.analyze(
            str(moving), ROI, {"camera_compensation": False, "scene_cut_detection": False,
                               "max_frames": 40})
        check("max_frames begrenzt die Anzahl Frames",
              stats_short["total_frames"] <= 40, str(stats_short["total_frames"]))

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
