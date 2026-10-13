"""Regressionstest für `auto_roi.find_two_rois` (Priorität 3 in
docs/NEXT.md: "improve automatic two-ROI suggestions").

`find_two_rois` war laut eigenem Docstring GEMESSEN UNZUREICHEND: an einem
Testvideo mit zwei nur rund 25px auseinanderliegenden Objekten erreichte
die automatische Regionswahl nur r=+0.17 bis +0.28 gegen den bekannten
Abstand, gegen +0.62 bei von Hand gewählten Regionen - weil einzelne
Raster-Zellen als Kandidaten dienten und die Regel "räumlich getrennt"
genau die Zellpaare ausschloss, die je ein Objekt am besten trafen.

Ersetzt durch `_peak_regions()`: zusammenhängende Bewegungsregionen um die
jeweils stärkste noch unverbrauchte Zelle, nacheinander gebildet (die
stärkere Region wird aus der Zellenmenge entfernt, bevor die nächste
gesucht wird) - dadurch bekommt jedes Objekt seine eigene Region statt
beide in einem Schwellwert-Klumpen zu verschmelzen oder auf eine einzelne
Zelle reduziert zu werden.

Zwei Szenen, beide mit Kameraschwenk + gemeinsamer Bewegung + schwingendem
Abstand als Signal (wie two_point_test.py, das dieselbe Grundkonstruktion
für die Distanzmessung selbst nutzt - hier zusätzlich Abstand *anfangs
automatischer ROI-Wahl*, nicht per Hand vorgegeben):

  - eng beieinander (~25px, reproduziert den Fall aus dem alten Docstring,
    der die automatische Wahl am schlechtesten abschneiden ließ)
  - weiter auseinander (two_point_test.py's eigene Fixture, ROI_A/ROI_B)

Ausführen: python3 generator/auto_roi_two_point_test.py
"""

import sys
import tempfile
from pathlib import Path

import cv2
import numpy as np

import auto_roi
from generate_funscript import track_two_points
from two_point_test import write_video as write_separated_video
from two_point_test import correlation

W, H, FPS, SECONDS = 320, 240, 25, 12
GAP_AMPLITUDE = 30.0
RADIUS = 16


def write_close_video(path, seed=11, close_gap=25):
    """Zwei Objekte, deren VERTIKALER Abstand das Signal ist (find_roi/
    find_two_rois werten nur vertikalen Flow aus), close_gap ist der
    Rand-zu-Rand-Abstand bei mittlerer Auslenkung - nah am ~25px-Fall aus
    find_two_rois' Docstring."""
    rng = np.random.default_rng(seed)
    pad = 140
    bg = np.full((H + pad, W + pad, 3), 40, np.uint8)
    for _ in range(220):
        x, y = int(rng.integers(0, W + pad)), int(rng.integers(0, H + pad))
        color = tuple(int(v) for v in rng.integers(60, 190, 3))
        cv2.rectangle(bg, (x, y), (x + int(rng.integers(8, 24)), y + int(rng.integers(8, 24))),
                      color, -1)
    bg = cv2.GaussianBlur(bg, (3, 3), 0)

    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    truth = []
    base_gap = RADIUS * 2 + close_gap
    world_cx, world_cy = 210, 180
    for i in range(FPS * SECONDS):
        t = i / FPS
        off_x = int(60 + 35 * np.sin(2 * np.pi * t * 0.09))
        off_y = int(60 + 25 * np.sin(2 * np.pi * t * 0.07))
        frame = bg[off_y:off_y + H, off_x:off_x + W].copy()
        common = 5 * np.sin(2 * np.pi * t * 0.15)
        gap = base_gap + (GAP_AMPLITUDE / 2) * np.sin(2 * np.pi * t)
        ay = world_cy + common - gap / 2
        by = world_cy + common + gap / 2
        cv2.circle(frame, (int(world_cx - off_x), int(ay - off_y)), RADIUS, (250, 250, 250), -1)
        cv2.circle(frame, (int(world_cx - off_x), int(by - off_y)), RADIUS, (210, 210, 210), -1)
        truth.append(gap)
        vw.write(frame)
    vw.release()
    return np.asarray(truth)


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory() as tmp:
        # --- Eng beieinander: der ursprünglich schlechteste Fall ----------
        close_video = Path(tmp) / "close.mp4"
        truth_close = write_close_video(close_video)

        roi_a, roi_b = auto_roi.find_two_rois(str(close_video), report_progress=False)
        check("eng beieinander: zwei gültige Boxen zurückgegeben",
              len(roi_a) == 4 and len(roi_b) == 4 and roi_a[2] > 0 and roi_b[2] > 0,
              f"{roi_a} {roi_b}")

        ts, dist, size, cuts, stats = track_two_points(str(close_video), roi_a, roi_b)
        r_close = correlation(dist, truth_close)
        # Gemessen (siehe Modul-Docstring): alt +0.01, neu +0.63 - Schwelle
        # deutlich unter dem gemessenen Wert, um nicht bei jeder kleinen
        # Rauschschwankung zu reißen, aber weit über dem alten Ergebnis.
        check("eng beieinander: automatische Regionen finden das Signal",
              r_close > 0.4, f"r={r_close:+.3f}")

        # --- Weiter auseinander: darf durch die Umstellung nicht schlechter
        #     werden als vorher (two_point_test.py's eigene Fixture) -------
        sep_video = Path(tmp) / "sep.mp4"
        truth_sep = write_separated_video(sep_video)

        roi_a2, roi_b2 = auto_roi.find_two_rois(str(sep_video), report_progress=False)
        ts2, dist2, size2, cuts2, stats2 = track_two_points(str(sep_video), roi_a2, roi_b2)
        r_sep = correlation(dist2, truth_sep)
        # Gemessen: alt +0.17, neu +0.21 - Schwelle über dem alten Ergebnis.
        check("weiter auseinander: mindestens so gut wie das alte Verfahren",
              r_sep > 0.17, f"r={r_sep:+.3f}")

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
