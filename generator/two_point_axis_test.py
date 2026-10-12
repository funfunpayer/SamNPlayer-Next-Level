"""Regressionstest für einen echten Tracking-Bug, gefunden bei der
Untersuchung der schwachen FunGen-Korrelation aus Issue #8 und
docs/FUNGEN_PARITY_PLAN.md (Arbeitspaket 2: "Investigate ... Its comments
claim zoom invariance, but pixel distance changes under zoom: test that
claim and correct it").

track_two_points() maß den Abstand früher NUR über die Y-Koordinate der
beiden Boxmittelpunkte (`box[1] + box[3] / 2.0`), nicht als vollständigen
2D-Abstand. Das ist harmlos, solange ROI1 und ROI2 unterschiedliche Höhen
haben (der bisherige two_point_test.py deckt genau diesen Fall ab). Es wird
aber zum stillen Nullsignal, sobald ROI2 auf GLEICHER Höhe wie ROI1 liegt -
exakt der Fall, den Issue #8 für den realen Batch beschreibt: "ROI2
invented as heuristic shift (~1.2x width to the right)", also ein reiner
Seitversatz ohne Höhenänderung. Ein Verfahren, das nur die Y-Achse ansieht,
sieht dann unabhängig vom tatsächlichen (horizontalen) Abstand nahezu
konstant Null - genau das Muster, das in den echten SamNPlayer-tj-Ausgaben
("BBW...", "Entladen...") zu sehen war: über weite Strecken ein praktisch
unbewegtes Signal.

Fix: der Abstand wird jetzt als 2D-Abstand der Boxmittelpunkte berechnet
(np.hypot), das deckt sowohl vertikale als auch horizontale (und diagonale)
Konfigurationen ab, ohne den bereits bestehenden vertikalen Testfall zu
verschlechtern.

Ausführen: python3 generator/two_point_axis_test.py
"""

import sys
import tempfile
from pathlib import Path

import cv2
import numpy as np

import generate_funscript as g

W, H, FPS, SECONDS = 320, 240, 25, 10
GAP_AMPLITUDE = 80.0


def write_horizontal_gap_video(path):
    """Zwei Objekte auf GLEICHER Höhe (y), deren Abstand rein waagerecht
    schwingt - wie ROI2 aus einer reinen Seitversatz-Heuristik entstehen
    würde (Issue #8: "shift ROI1 by ~1.2x width")."""
    rng = np.random.default_rng(7)
    bg = np.full((H, W, 3), 40, np.uint8)
    for _ in range(150):
        x, y = int(rng.integers(0, W)), int(rng.integers(0, H))
        color = tuple(int(v) for v in rng.integers(60, 190, 3))
        cv2.rectangle(bg, (x, y), (x + 12, y + 12), color, -1)
    bg = cv2.GaussianBlur(bg, (3, 3), 0)

    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    truth = []
    y_fixed = 120
    for i in range(FPS * SECONDS):
        t = i / FPS
        frame = bg.copy()
        # Basis 80 statt z.B. 60, damit die beiden Kreise (Radius 14) beim
        # kleinsten Abstand (80-40=40) nie überlappen - Überlappung ist ein
        # bekanntes, DAVON UNABHÄNGIGES Tracking-Problem (siehe
        # track_two_points-Docstring) und würde diesen Test verfälschen.
        gap = 80 + (GAP_AMPLITUDE / 2) * np.sin(2 * np.pi * t)  # das echte Signal
        cv2.circle(frame, (90, y_fixed), 14, (250, 250, 250), -1)
        cv2.circle(frame, (int(90 + gap), y_fixed), 14, (210, 210, 210), -1)
        truth.append(gap)
        vw.write(frame)
    vw.release()
    return np.asarray(truth)


def correlation(signal, truth):
    n = min(len(signal), len(truth))
    return float(np.corrcoef(signal[:n], truth[:n])[0, 1])


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory() as tmp:
        video = Path(tmp) / "horizontal_gap.mp4"
        truth = write_horizontal_gap_video(video)

        # ROI2 mit GLEICHER y-Koordinate wie ROI1, nur seitlich versetzt -
        # exakt die aus Issue #8 beschriebene Batch-Heuristik.
        roi_a = (86, 106, 28, 28)
        roi_b = (86 + 55, 106, 28, 28)

        ts, distances, size, cuts, stats = g.track_two_points(str(video), roi_a, roi_b)

        check("Rückgabeform passt zu track_roi",
              len(ts) == len(distances) and size == (W, H) and isinstance(stats, dict))
        check("Abstände sind nie negativ", float(distances.min()) >= 0)

        corr = correlation(distances, truth)
        check("2D-Abstand findet ein rein waagerechtes Signal "
              "(vorher: nahezu Null, weil nur die Y-Koordinate verglichen wurde)",
              corr > 0.7, f"corr={corr:+.3f}")
        check("Amplitude in der richtigen Größenordnung",
              abs(np.ptp(distances) - GAP_AMPLITUDE) < 25,
              f"{np.ptp(distances):.1f} statt {GAP_AMPLITUDE}")

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
