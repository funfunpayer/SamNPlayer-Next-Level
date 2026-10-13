"""Regressionstest für `auto_roi._peak_regions`'s Größenbegrenzung.

Gefunden beim Testen mit echtem Material (docs/NEXT.md Priorität 3, "Run
against the real clip", 15. September 2026): auf einem sauberen
synthetischen Testvideo bleibt eine gewachsene Region von selbst kompakt,
weil außerhalb der beiden Objekte kaum etwas über der `decay`-Schwelle
liegt. Echtes Material hat dagegen verbreitete, kamerakompensierte
Restbewegung über viele Zellen hinweg - `find_two_rois()` auf dem echten
`clip_h264.mp4` schlug dadurch `ROI1=(0,0,256,144)` vor, das GESAMTE Bild.

Dieser Test braucht kein Video - er konstruiert direkt ein Score-Raster,
das genau dieses Muster nachbildet (zwei klare Bewegungsspitzen plus
diffuses Hintergrundrauschen knapp über der Schwelle über einen Großteil
des Rasters), und prüft nur die Geometrie der gefundenen Regionen, nicht
die Tracking-Qualität (die ist bereits durch auto_roi_two_point_test.py
abgedeckt).

Ausführen: python3 generator/auto_roi_region_bounds_test.py
"""

import sys

import numpy as np

import auto_roi


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    rows, cols = 8, 12
    rng = np.random.default_rng(3)
    # Diffuses Hintergrundrauschen knapp über der decay-Schwelle (0.4 * Peak)
    # über fast das GESAMTE Raster - genau das reale Muster, das die
    # Bestensuche ohne Obergrenze quer durchs ganze Bild wandern ließ.
    peak_value = 1.0
    scores = rng.uniform(0.41, 0.55, size=(rows, cols)) * peak_value
    # Zwei klare, räumlich getrennte Bewegungsspitzen.
    scores[1, 2] = peak_value
    scores[6, 9] = peak_value * 0.95

    regions = auto_roi._peak_regions(scores)
    check("mindestens zwei Regionen gefunden", len(regions) >= 2, str(len(regions)))

    max_row_span = max(2, rows // 3)
    max_col_span = max(2, cols // 3)
    for i, cells in enumerate(regions[:2]):
        rs = [r for r, c in cells]
        cs = [c for r, c in cells]
        row_span = max(rs) - min(rs) + 1
        col_span = max(cs) - min(cs) + 1
        check(f"Region {i}: Zeilen-Ausdehnung bleibt begrenzt",
              row_span <= max_row_span, f"{row_span} > {max_row_span}")
        check(f"Region {i}: Spalten-Ausdehnung bleibt begrenzt",
              col_span <= max_col_span, f"{col_span} > {max_col_span}")
        area_frac = (row_span * col_span) / (rows * cols)
        check(f"Region {i}: deckt nicht das halbe Raster ab",
              area_frac < 0.5, f"{area_frac:.2f}")

    # Die beiden stärksten Spitzen müssen in unterschiedlichen Regionen
    # landen (nicht dieselbe Region auffressen, nur weil beide erreichbar
    # wären).
    peak_a_region = next((i for i, c in enumerate(regions) if (1, 2) in c), None)
    peak_b_region = next((i for i, c in enumerate(regions) if (6, 9) in c), None)
    check("beide Bewegungsspitzen gefunden",
          peak_a_region is not None and peak_b_region is not None,
          f"{peak_a_region}, {peak_b_region}")
    check("beide Bewegungsspitzen in getrennten Regionen",
          peak_a_region != peak_b_region and None not in (peak_a_region, peak_b_region),
          f"{peak_a_region}, {peak_b_region}")

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
