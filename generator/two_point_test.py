"""Regressionstest für die Zwei-Punkt-Messung.

Der Kerngedanke ist mathematisch, nicht heuristisch: der Abstand zwischen
zwei Punkten im selben Bild ist von Kamerabewegung unabhängig. Schwenkt oder
zoomt die Kamera, verschieben sich beide Punkte gemeinsam - der Abstand
bleibt. Das Problem entsteht gar nicht erst und muss nicht nachträglich
herausgerechnet werden.

Ebenso fällt gemeinsame Bewegung beider Objekte heraus, die eine
Einzelpunktmessung fälschlich als Signal sähe.

Das Testvideo enthält deshalb bewusst alle drei Anteile: Kameraschwenk,
gemeinsame Auf-Ab-Bewegung, und einen schwingenden Abstand als eigentliches
Signal. Nur wer den Abstand misst, findet das Signal.

Dauer: rund eine Minute. Ausführen:
  python3 generator/two_point_test.py
"""

import sys
import tempfile
from pathlib import Path

import cv2
import numpy as np

import generate_funscript as g

W, H, FPS, SECONDS = 320, 240, 25, 14
GAP_AMPLITUDE = 80.0
ROI_A = (134, 54, 32, 32)
ROI_B = (159, 104, 32, 32)


def write_video(path):
    """Zwei Objekte in Weltkoordinaten, dazu Kameraschwenk.

    Die Objekte müssen in WELTkoordinaten liegen und im Bild mitwandern -
    an fester Bildschirmposition gezeichnet machten sie den Schwenk nicht
    mit, und der Vorteil der Abstandsmessung wäre nicht prüfbar.
    """
    rng = np.random.default_rng(21)
    bg = np.full((H + 140, W + 140, 3), 40, np.uint8)
    for _ in range(200):
        x, y = int(rng.integers(0, W + 140)), int(rng.integers(0, H + 140))
        color = tuple(int(v) for v in rng.integers(60, 190, 3))
        cv2.rectangle(bg, (x, y), (x + int(rng.integers(8, 24)), y + int(rng.integers(8, 24))),
                      color, -1)
    bg = cv2.GaussianBlur(bg, (3, 3), 0)

    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    truth = []
    for i in range(FPS * SECONDS):
        t = i / FPS
        off_x = int(70 + 45 * np.sin(2 * np.pi * t * 0.13))
        off_y = int(70 + 30 * np.sin(2 * np.pi * t * 0.11))
        frame = bg[off_y:off_y + H, off_x:off_x + W].copy()
        common = 70 + 25 * np.sin(2 * np.pi * t * 0.2)      # gemeinsame Bewegung
        gap = 70 + (GAP_AMPLITUDE / 2) * np.sin(2 * np.pi * t)  # das echte Signal
        cv2.circle(frame, (150, int(70 + common - gap / 2 - off_y)), 16, (250, 250, 250), -1)
        cv2.circle(frame, (175, int(70 + common + gap / 2 - off_y)), 16, (210, 210, 210), -1)
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
        video = Path(tmp) / "two_point.mp4"
        truth = write_video(video)

        ts, distances, size, cuts, stats = g.track_two_points(str(video), ROI_A, ROI_B)

        check("Rückgabeform passt zu track_roi",
              len(ts) == len(distances) and size == (W, H) and isinstance(stats, dict))
        check("Zeitstempel streng aufsteigend", bool(np.all(np.diff(ts) > 0)))
        check("Abstände sind nie negativ", float(distances.min()) >= 0)

        two_point_corr = correlation(distances, truth)
        check("Abstand folgt dem echten Signal", two_point_corr > 0.5, f"{two_point_corr:+.3f}")
        check("Amplitude in der richtigen Größenordnung",
              abs(np.ptp(distances) - GAP_AMPLITUDE) < 40,
              f"{np.ptp(distances):.1f} statt {GAP_AMPLITUDE}")

        # Der entscheidende Vergleich: eine Einzelpunktmessung darf das
        # Signal hier NICHT finden. Täte sie es, wäre das Testvideo zu
        # einfach und der Test würde nichts beweisen.
        _, single, _, _, _ = g.track_roi(str(video), ROI_A, camera_compensation=False)
        single_corr = correlation(single, truth)
        check("Einzelpunktmessung findet das Signal nicht",
              abs(single_corr) < 0.4, f"{single_corr:+.3f}")
        check("Zwei-Punkt-Messung ist deutlich besser",
              two_point_corr > abs(single_corr) + 0.3,
              f"zwei {two_point_corr:+.3f} gegen ein {single_corr:+.3f}")

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
