"""Regressionstest für grid_lk_backend.analyze_two_point().

--backend grid_lk wurde bisher stillschweigend ignoriert, sobald --roi2
gesetzt war - die Zwei-Punkt-Messung lief immer über track_two_points()
(fest verdrahtetes CSRT), unabhängig von --backend. Derselbe "stille
Backend-Fall" wie schon einmal bei der GUI-Anbindung (#45/#46), hier für
den CLI-Zweipunktpfad; gefunden beim Versuch, grid_lk für Tf/Tj zu messen
(docs/NEXT.md Abschnitt 8) - zwei aufeinanderfolgende Läufe mit
unterschiedlichem --backend erzeugten byte-identische Ausgaben.

Dieser Test prüft direkt gegen dasselbe Testvideo wie two_point_test.py
(Kameraschwenk + gemeinsame Bewegung + schwingender Abstand als Signal),
damit beide Zwei-Punkt-Pfade an derselben Messlatte stehen: analyze_two_point
muss das Signal finden, nicht nur irgendein Signal liefern.

Ausführen:  python3 generator/grid_lk_two_point_test.py
"""

import sys
import tempfile
from pathlib import Path

import numpy as np

import grid_lk_backend
from two_point_test import ROI_A, ROI_B, W, H, correlation, write_video, GAP_AMPLITUDE


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory() as tmp:
        video = Path(tmp) / "two_point.mp4"
        truth = write_video(video)

        ts, distances, size, cuts, stats = grid_lk_backend.analyze_two_point(
            str(video), ROI_A, ROI_B, {})

        check("Rückgabeform passt zu track_two_points",
              len(ts) == len(distances) and size == (W, H) and isinstance(stats, dict))
        check("Zeitstempel streng aufsteigend", bool(np.all(np.diff(ts) > 0)))
        check("Abstände sind nie negativ", float(distances.min()) >= 0)
        check("stats enthält tracker_lost_frames", "tracker_lost_frames" in stats)

        corr = correlation(distances, truth)
        check("Abstand folgt dem echten Signal", corr > 0.5, f"{corr:+.3f}")
        check("Amplitude in der richtigen Größenordnung",
              abs(np.ptp(distances) - GAP_AMPLITUDE) < 40,
              f"{np.ptp(distances):.1f} statt {GAP_AMPLITUDE}")

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
