"""Regressionstest für die Kamerabewegungs-Kompensation.

Erzeugt zwei kurze Testvideos mit einem realistisch texturierten Hintergrund
und prüft die zwei Fälle, auf die es ankommt:

  1. Stehende Kamera -> die Kompensation darf das Signal NICHT verändern.
  2. Schwenkende Kamera -> die Kompensation muss den Schwenk herausrechnen
     und die echte Objektamplitude wiederherstellen.

Wichtig für künftige Änderungen an diesem Test: der Hintergrund MUSS
verfolgbare Ecken und Kanten haben. Mit reinem Rauschen findet
goodFeaturesToTrack keine stabilen Merkmale, die Schätzung liefert pro Frame
Zufallswerte, und weil die Kompensation diese Werte aufsummiert, entsteht ein
Random Walk. Das sieht dann nach einem Fehler in der Kompensation aus, ist
aber ein Fehler im Testmaterial - genau dieser Irrtum ist schon einmal
passiert.

Ebenso wichtig: das bewegte Objekt muss in WELTkoordinaten liegen, also bei
einem Schwenk im Bild mitwandern. Zeichnet man es an fester Bildschirmposition,
macht es den Schwenk nicht mit, und die Kompensation "verschlimmbessert"
scheinbar - ebenfalls ein Testfehler, kein Codefehler.

Dauer: rund zwei Minuten (Videodekodierung). Ausführen:
  python3 generator/camera_compensation_test.py
"""

import sys
import tempfile
from pathlib import Path

import cv2
import numpy as np
from scipy.signal import savgol_filter

import generate_funscript as g

W, H, FPS, SECONDS = 320, 240, 25, 12
ROI = (138, 98, 44, 44)
TRUE_AMPLITUDE_PX = 110.0


def textured_background():
    """Hintergrund mit verfolgbaren Merkmalen - siehe Modulkommentar."""
    rng = np.random.default_rng(11)
    bg = np.full((H * 2, W * 2, 3), 40, np.uint8)
    for _ in range(160):
        x, y = int(rng.integers(0, W * 2)), int(rng.integers(0, H * 2))
        color = tuple(int(v) for v in rng.integers(60, 200, 3))
        if rng.random() < 0.5:
            cv2.rectangle(bg, (x, y), (x + int(rng.integers(8, 26)), y + int(rng.integers(8, 26))), color, -1)
        else:
            cv2.circle(bg, (x, y), int(rng.integers(4, 13)), color, -1)
    return cv2.GaussianBlur(bg, (3, 3), 0)


def write_video(path, pan):
    bg = textured_background()
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    for i in range(FPS * SECONDS):
        t = i / FPS
        off_x = int(60 + (40 * np.sin(2 * np.pi * t * 0.12) if pan else 0))
        off_y = int(60 + (25 * np.sin(2 * np.pi * t * 0.09) if pan else 0))
        frame = bg[off_y:off_y + H, off_x:off_x + W].copy()
        world_y = 60 + 120 + (TRUE_AMPLITUDE_PX / 2) * np.sin(2 * np.pi * t)
        cv2.circle(frame, (160, int(world_y - off_y)), 22, (250, 250, 250), -1)
        vw.write(frame)
    vw.release()


def amplitude(video, camera_compensation):
    _, y, _, _, _ = g.track_roi(str(video), ROI, camera_compensation=camera_compensation)
    return float(np.ptp(savgol_filter(y, 11, 3)))


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)

        static_video = tmp / "static_cam.mp4"
        write_video(static_video, pan=False)
        raw = amplitude(static_video, False)
        comp = amplitude(static_video, True)
        check("stehende Kamera: Rohkurve trifft die echte Amplitude",
              abs(raw - TRUE_AMPLITUDE_PX) < 20, f"{raw:.1f} statt {TRUE_AMPLITUDE_PX}")
        check("stehende Kamera: Kompensation verändert das Signal nicht",
              abs(comp - raw) < 8, f"roh {raw:.1f} vs kompensiert {comp:.1f}")

        pan_video = tmp / "panning_cam.mp4"
        write_video(pan_video, pan=True)
        raw_pan = amplitude(pan_video, False)
        comp_pan = amplitude(pan_video, True)
        check("Schwenk verfälscht die unkompensierte Kurve deutlich",
              raw_pan > TRUE_AMPLITUDE_PX + 25, f"{raw_pan:.1f}")
        check("Kompensation stellt die echte Amplitude wieder her",
              abs(comp_pan - TRUE_AMPLITUDE_PX) < 20, f"{comp_pan:.1f} statt {TRUE_AMPLITUDE_PX}")
        check("Kompensation ist beim Schwenk klar besser als ohne",
              abs(comp_pan - TRUE_AMPLITUDE_PX) < abs(raw_pan - TRUE_AMPLITUDE_PX),
              f"mit {comp_pan:.1f}, ohne {raw_pan:.1f}")

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
