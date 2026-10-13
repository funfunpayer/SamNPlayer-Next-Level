"""Regressionstest für das Grid-LK-Backend.

Prüft die Eigenschaften, auf denen der Entwurf beruht (siehe Modulkommentar
in grid_lk_backend.py und docs/NEXT.md Abschnitt 8 für die Messung an
echtem Material, auf der die Gitterdichte GRID_N/GRID_M beruht):

  * Vertragsform wie track_roi()/flow_backend.analyze (dieselbe Prüfung wie
    backends.py sie zur Laufzeit macht).
  * Die Amplitude einer echten Bewegung muss ungefähr stimmen (Median der
    Gitterpunkte folgt dem Objekt).
  * GRACEFUL DEGRADATION - der eigentliche Kern der Idee: wenn ein Teil der
    Region unbeweglich/texturlos ist (weniger Gitterpunkte finden dort echte
    Bewegung), darf das Signal nicht einfach verschwinden - der Median der
    verbliebenen guten Punkte soll die Bewegung weiterhin zeigen.
  * VOLLSTÄNDIGER Verlust (0 überlebende Punkte, z.B. bei einem harten
    Schnitt) wird gezählt (tracker_lost_frames) und die letzte Position
    fortgeschrieben, statt zu raten - und das Gitter erholt sich danach
    wieder, statt dauerhaft eingefroren zu bleiben.
  * Kamerakompensation und Achsenwahl funktionieren wie bei den anderen
    Backends.

Dauer: rund zwei Minuten. Ausführen:
  python3 generator/grid_lk_backend_test.py
"""

import sys
import tempfile
from pathlib import Path

import cv2
import numpy as np

import grid_lk_backend

W, H, FPS = 320, 240, 25
TRUE_AMPLITUDE = 80.0
ROI = (110, 60, 100, 100)  # umschließt die Kreisbewegung unten großzügig


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


def write_moving_half_flat(path, seconds=8, freq=1.0):
    """Wie write_moving, aber die UNTERE Hälfte der ROI ist eine flache,
    texturlose Fläche - goodFeaturesToTrack/LK finden dort kaum brauchbare
    Punkte. Prüft, dass der Median der verbliebenen (oberen) Punkte die
    Bewegung noch zeigt, statt dass das ganze Signal verrauscht/verschwindet.
    """
    bg = textured_background()
    x, y, w, h = ROI
    flat_y0 = y + h // 2
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    for i in range(FPS * seconds):
        t = i / FPS
        frame = bg.copy()
        frame[flat_y0:, :] = 90  # texturlose Fläche über die untere ROI-Hälfte
        cy = int(160 + (TRUE_AMPLITUDE / 2) * np.sin(2 * np.pi * freq * t))
        cv2.circle(frame, (160, cy), 35, (250, 250, 250), -1)
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
        world_y = 60 + 160 + (TRUE_AMPLITUDE / 2) * np.sin(2 * np.pi * freq * t)
        cv2.circle(frame, (160, int(world_y - off_y)), 35, (250, 250, 250), -1)
        vw.write(frame)
    vw.release()


def write_with_hard_cut(path, seconds=8, cut_at=100):
    """Bewegtes Objekt, aber bei Frame cut_at ein harter Szenenschnitt (ganz
    andere Bildkomposition) - das Gitter muss dort neu gesät werden und sich
    danach wieder auf die (fortgesetzte) Bewegung einschwingen."""
    bg1 = textured_background(seed=11)
    bg2 = textured_background(seed=99)
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    n = FPS * seconds
    for i in range(n):
        t = i / FPS
        bg = bg1 if i < cut_at else bg2
        frame = bg.copy()
        if i >= cut_at:
            # zusätzlicher Kontrastblock, damit detect_scene_cut sicher anschlägt
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
        ts, pos, size, cuts, stats = grid_lk_backend.analyze(
            str(moving), ROI, {"camera_compensation": False, "scene_cut_detection": False})

        check("liefert dieselbe Rückgabeform wie track_roi/flow_backend",
              len(ts) == len(pos) and size == (W, H) and isinstance(stats, dict))
        for key in ("tracker_lost_frames", "camera_frames_lost", "total_frames",
                   "vertical_range", "horizontal_range"):
            check(f"stats enthält Pflichtfeld {key!r}", key in stats)
        check("Zeitstempel streng aufsteigend", bool(np.all(np.diff(ts) > 0)))
        check("Amplitude trifft die echte Bewegung ungefähr",
              abs(np.ptp(pos) - TRUE_AMPLITUDE) < 25,
              f"{np.ptp(pos):.1f} statt {TRUE_AMPLITUDE}")
        check("praktisch keine komplett verlorenen Frames auf einfachem Material",
              stats["tracker_lost_frames"] / stats["total_frames"] < 0.05,
              str(stats["tracker_lost_frames"]))
        check("Gitter behält fast den vollen Punktbestand",
              stats["grid_mean_survivors"] > stats["grid_target_points"] * 0.8,
              f"{stats['grid_mean_survivors']}/{stats['grid_target_points']}")

        # --- Kernhypothese: graceful degradation ---------------------------
        # Weniger überlebende Punkte (texturlose halbe ROI) darf die Kurve
        # NICHT zum Verschwinden bringen - der Median der restlichen Punkte
        # soll die Bewegung noch zeigen, deutlich besser als "eingefroren".
        half_flat = tmp / "half_flat.mp4"
        write_moving_half_flat(half_flat, seconds=8)
        _, pos_hf, _, _, stats_hf = grid_lk_backend.analyze(
            str(half_flat), ROI, {"camera_compensation": False, "scene_cut_detection": False})
        check("weniger Textur -> weniger überlebende Punkte im Schnitt",
              stats_hf["grid_mean_survivors"] < stats["grid_mean_survivors"],
              f"{stats_hf['grid_mean_survivors']} vs {stats['grid_mean_survivors']}")
        check("Bewegung bleibt trotzdem erkennbar (graceful, nicht eingefroren)",
              np.ptp(pos_hf) > TRUE_AMPLITUDE * 0.35,
              f"{np.ptp(pos_hf):.1f} statt mind. {TRUE_AMPLITUDE * 0.35:.1f}")
        check("nicht die Mehrheit der Frames komplett verloren bei nur halb texturloser ROI",
              stats_hf["tracker_lost_frames"] / stats_hf["total_frames"] < 0.5,
              str(stats_hf["tracker_lost_frames"]))

        # --- Vollständiger, aber vorübergehender Verlust am Szenenschnitt --
        cut_video = tmp / "cut.mp4"
        write_with_hard_cut(cut_video, seconds=8, cut_at=100)
        _, pos_cut, _, cuts_cut, stats_cut = grid_lk_backend.analyze(
            str(cut_video), ROI, {"camera_compensation": False, "scene_cut_detection": True})
        check("Szenenschnitt wird erkannt", len(cuts_cut) >= 1, str(cuts_cut))
        check("Gitter erholt sich nach dem Schnitt (Bewegung weiterhin sichtbar)",
              np.ptp(pos_cut[110:]) > TRUE_AMPLITUDE * 0.3,
              f"{np.ptp(pos_cut[110:]):.1f}")
        check("Signal bleibt im Bildbereich, kein Wegdriften nach dem Schnitt",
              float(pos_cut.min()) >= -20 and float(pos_cut.max()) <= H + 20,
              f"{pos_cut.min():.1f}..{pos_cut.max():.1f}")

        # --- Kamerakompensation: Position gegen Position --------------------
        panning = tmp / "panning.mp4"
        write_moving_with_pan(panning, seconds=10)
        _, pos_pan, _, _, _ = grid_lk_backend.analyze(
            str(panning), ROI, {"camera_compensation": True, "scene_cut_detection": False})
        overshoot = float(np.ptp(pos_pan))
        check("Amplitude bei Schwenk in vertretbarem Rahmen (mit Kompensation)",
              overshoot < TRUE_AMPLITUDE * 1.6,
              f"{overshoot:.1f} statt {TRUE_AMPLITUDE}")

        _, pos_pan_uncorrected, _, _, _ = grid_lk_backend.analyze(
            str(panning), ROI, {"camera_compensation": False, "scene_cut_detection": False})
        check("Kamerakorrektur verbessert die Amplitude messbar",
              abs(np.ptp(pos_pan_uncorrected) - TRUE_AMPLITUDE) > abs(overshoot - TRUE_AMPLITUDE),
              f"ohne {np.ptp(pos_pan_uncorrected):.1f}, mit {overshoot:.1f}")

        # --- Achsenwahl ------------------------------------------------------
        _, pos_x, _, _, _ = grid_lk_backend.analyze(
            str(moving), ROI, {"camera_compensation": False, "scene_cut_detection": False,
                               "axis": "x"})
        check("waagerechte Achse liefert bei senkrechter Bewegung wenig Ausschlag",
              float(np.ptp(pos_x)) < float(np.ptp(pos)),
              f"x {np.ptp(pos_x):.1f} vs y {np.ptp(pos):.1f}")

        # --- max_frames wird respektiert -------------------------------------
        _, pos_short, _, _, stats_short = grid_lk_backend.analyze(
            str(moving), ROI, {"camera_compensation": False, "scene_cut_detection": False,
                               "max_frames": 40})
        check("max_frames begrenzt die Anzahl Frames",
              stats_short["total_frames"] <= 40, str(stats_short["total_frames"]))

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
