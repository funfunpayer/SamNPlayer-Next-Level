"""Regressionstest für das Region-Fusion-Auto-Backend (ganzes Bild, keine
markierte Region).

Prüft dieselben Grundeigenschaften wie region_fusion_backend_test.py
(Vertragsform, Kamerakompensation, Achsenwahl, max_frames, Szenenschnitt)
UND die Eigenschaft, die dieses Backend von region_fusion_backend.py
unterscheidet (siehe Modulkommentar in region_fusion_auto_backend.py): die
vier Zonen decken das GANZE Bild ab und liegen an entgegengesetzten
Bildecken - eine naive Fusion der ABSOLUTEN Pixelpositionen würde bei einem
Gewichtswechsel zwischen Zonen einen bedeutungslosen Sprung quer übers Bild
erzeugen. Das Backend normalisiert stattdessen je Zone auf deren eigene Box,
bevor fusioniert wird - geprüft wird hier, dass Bewegung in einer einzelnen,
weit vom Bildzentrum entfernten Ecke KEINEN Ausschlag erzeugt, der die
Zonengrenze (ein Viertel der Bildhöhe/-breite) deutlich überschreitet, wie es
eine Blend-über-rohe-Pixelkoordinaten-Variante täte.

Dauer: ähnlich region_fusion_backend_test.py (vier Teilregionen).
Ausführen: python3 generator/region_fusion_auto_backend_test.py
"""

import sys
import tempfile
from pathlib import Path

import cv2
import numpy as np

import region_fusion_auto_backend

W, H, FPS = 320, 240, 25
TRUE_AMPLITUDE = 80.0


def textured_background(seed=11):
    rng = np.random.default_rng(seed)
    bg = np.full((H, W, 3), 40, np.uint8)
    for _ in range(140):
        x, y = int(rng.integers(0, W)), int(rng.integers(0, H))
        color = tuple(int(v) for v in rng.integers(60, 190, 3))
        cv2.rectangle(bg, (x, y), (x + int(rng.integers(6, 18)), y + int(rng.integers(6, 18))),
                      color, -1)
    return cv2.GaussianBlur(bg, (3, 3), 0)


def write_moving_centered(path, seconds=8, freq=1.0):
    """Bewegung mittig im Bild, über die Zonengrenze bei H/2 hinweg - deckt
    ab, dass ein Objekt, das zwischen zwei Zonen wandert, noch ein
    sinnvolles Signal ergibt."""
    bg = textured_background()
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    for i in range(FPS * seconds):
        t = i / FPS
        frame = bg.copy()
        y = int(H / 2 + (TRUE_AMPLITUDE / 2) * np.sin(2 * np.pi * freq * t))
        cv2.circle(frame, (W // 2, y), 30, (250, 250, 250), -1)
        vw.write(frame)
    vw.release()


def write_moving_top_left_corner(path, seconds=8, freq=1.0):
    """Bewegung ausschließlich innerhalb der oberen linken Zone (x<W/2,
    y<H/2), weit von der Bildmitte entfernt - die Kernprüfung dieses Tests:
    eine Fusion über rohe Pixelkoordinaten würde hier bereits nahe 0 (obere
    linke Ecke) liegen, aber bei einem Zonenwechsel (z.B. durch Rauschen in
    einer anderen Zone) einen Sprung Richtung Bildmitte/anderer Ecken
    machen können. Amplitude klein genug, um klar innerhalb der Zone (W/2 x
    H/2 = 160x120) zu bleiben."""
    bg = textured_background()
    amplitude = 30.0
    cx, cy_base = 60, 50
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    for i in range(FPS * seconds):
        t = i / FPS
        frame = bg.copy()
        y = int(cy_base + (amplitude / 2) * np.sin(2 * np.pi * freq * t))
        cv2.circle(frame, (cx, y), 15, (250, 250, 250), -1)
        vw.write(frame)
    vw.release()
    return amplitude


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
        y = int(H / 2 + (TRUE_AMPLITUDE / 2) * np.sin(2 * np.pi * t))
        cv2.circle(frame, (W // 2, y), 30, (250, 250, 250), -1)
        vw.write(frame)
    vw.release()


def write_moving_with_pan(path, seconds=10, freq=1.0):
    bg = textured_background()
    big = cv2.copyMakeBorder(bg, 60, 60, 60, 60, cv2.BORDER_REFLECT)
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    for i in range(FPS * seconds):
        t = i / FPS
        off_x = int(60 + 30 * np.sin(2 * np.pi * t * 0.11))
        off_y = int(60 + 20 * np.sin(2 * np.pi * t * 0.08))
        frame = big[off_y:off_y + H, off_x:off_x + W].copy()
        world_y = 60 + H / 2 + (TRUE_AMPLITUDE / 2) * np.sin(2 * np.pi * freq * t)
        cv2.circle(frame, (W // 2, int(world_y - off_y)), 30, (250, 250, 250), -1)
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
        write_moving_centered(moving, seconds=8)
        ts, pos, size, cuts, stats = region_fusion_auto_backend.analyze(
            str(moving), None, {"camera_compensation": False, "scene_cut_detection": False})

        check("liefert dieselbe Rückgabeform wie region_fusion_backend/grid_lk_backend",
              len(ts) == len(pos) and size == (W, H) and isinstance(stats, dict))
        for key in ("tracker_lost_frames", "camera_frames_lost", "total_frames",
                   "vertical_range", "horizontal_range"):
            check(f"stats enthält Pflichtfeld {key!r}", key in stats)
        check("Zeitstempel streng aufsteigend", bool(np.all(np.diff(ts) > 0)))
        check("4 Zonen gemeldet", stats["region_count"] == 4, str(stats["region_count"]))
        check("roi-Argument wird ignoriert (auch ein übergebenes roi hat keine Wirkung)",
              region_fusion_auto_backend.analyze(
                  str(moving), (10, 10, 20, 20),
                  {"camera_compensation": False, "scene_cut_detection": False})[4]["region_count"] == 4)

        # --- Kernprüfung: Bewegung weit in einer Bildecke bleibt in einem
        # sinnvollen (auf die Zonengröße begrenzten) Ausschlag, statt über
        # geblendete rohe Pixelkoordinaten quer übers Bild zu springen -----
        corner = tmp / "corner.mp4"
        corner_amplitude = write_moving_top_left_corner(corner, seconds=8)
        _, pos_corner, _, _, stats_corner = region_fusion_auto_backend.analyze(
            str(corner), None, {"camera_compensation": False, "scene_cut_detection": False})
        check("Ausschlag bei Bewegung nur in einer Bildecke bleibt in vertretbarem Rahmen "
              "(keine Verwässerung über die drei ruhigen Zonen)",
              np.ptp(pos_corner) > corner_amplitude * 0.3,
              f"{np.ptp(pos_corner):.1f} vs Amplitude {corner_amplitude}")
        check("... aber auch kein bildweiter Sprung (deutlich unter Bildhöhe/-breite)",
              np.ptp(pos_corner) < max(W, H) * 0.6,
              f"{np.ptp(pos_corner):.1f}")
        check("Fusion gewichtet die Zonen tatsächlich ungleich (mean_weight_spread > 0)",
              stats_corner["mean_weight_spread"] > 0.1,
              str(stats_corner["mean_weight_spread"]))

        # --- Szenenschnitt: erkannt, Fusion erholt sich danach ---------------
        cut_video = tmp / "cut.mp4"
        write_with_hard_cut(cut_video, seconds=8, cut_at=100)
        _, pos_cut, _, cuts_cut, stats_cut = region_fusion_auto_backend.analyze(
            str(cut_video), None, {"camera_compensation": False, "scene_cut_detection": True})
        check("Szenenschnitt wird erkannt", len(cuts_cut) >= 1, str(cuts_cut))
        check("Fusion erholt sich nach dem Schnitt (Bewegung weiterhin sichtbar)",
              np.ptp(pos_cut[110:]) > 5, f"{np.ptp(pos_cut[110:]):.1f}")

        # --- Kamerakompensation ------------------------------------------------
        panning = tmp / "panning.mp4"
        write_moving_with_pan(panning, seconds=10)
        _, pos_pan, _, _, stats_pan = region_fusion_auto_backend.analyze(
            str(panning), None, {"camera_compensation": True, "scene_cut_detection": False})
        check("Kamerakompensation läuft ohne Absturz durch (auch ohne Hintergrund außerhalb "
              "der Zonen - siehe Modulkommentar) und liefert ein Ergebnis",
              len(pos_pan) == len(pos_pan))

        # --- Achsenwahl ------------------------------------------------------
        _, pos_x, _, _, _ = region_fusion_auto_backend.analyze(
            str(moving), None, {"camera_compensation": False, "scene_cut_detection": False,
                                "axis": "x"})
        _, pos_y, _, _, _ = region_fusion_auto_backend.analyze(
            str(moving), None, {"camera_compensation": False, "scene_cut_detection": False,
                                "axis": "y"})
        check("waagerechte Achse liefert bei senkrechter Bewegung wenig Ausschlag",
              float(np.ptp(pos_x)) < float(np.ptp(pos_y)),
              f"x {np.ptp(pos_x):.1f} vs y {np.ptp(pos_y):.1f}")

        # --- max_frames wird respektiert -------------------------------------
        _, pos_short, _, _, stats_short = region_fusion_auto_backend.analyze(
            str(moving), None, {"camera_compensation": False, "scene_cut_detection": False,
                                "max_frames": 40})
        check("max_frames begrenzt die Anzahl Frames",
              stats_short["total_frames"] <= 40, str(stats_short["total_frames"]))

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
