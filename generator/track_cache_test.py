"""Regressionstest für den Trackingergebnis-Cache.

Der Cache ist die Voraussetzung dafür, dass Signalparameter überhaupt
praktikabel ausprobiert werden können: Das Verfolgen der ROI kostet bei einem
20-Sekunden-Video rund eine halbe Minute, alles danach Sekundenbruchteile.

Zwei Eigenschaften müssen stimmen, und beide sind gefährlich, wenn sie
kippen:

  * Ein Treffer muss EXAKT dasselbe liefern wie eine Neuberechnung. Sonst
    hängt das Ergebnis davon ab, ob zufällig ein Cache existierte.
  * Der Schlüssel muss alles enthalten, was das Tracking beeinflusst. Fehlt
    etwas, liefert der Cache still das Ergebnis anderer Einstellungen - und
    man sucht den Fehler an der völlig falschen Stelle.

Der Test benutzt ein sehr kurzes Video, damit er in Sekunden statt Minuten
läuft. Ausführen:  python3 generator/track_cache_test.py
"""

import sys
import tempfile
import time
from pathlib import Path

import cv2
import numpy as np

import generate_funscript as g

W, H, FPS, SECONDS = 160, 120, 25, 3
ROI = (66, 38, 28, 28)


def write_video(path):
    rng = np.random.default_rng(3)
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    for i in range(FPS * SECONDS):
        frame = rng.integers(20, 45, (H, W, 3), dtype=np.uint8)
        y = int(60 + 28 * np.sin(2 * np.pi * i / FPS))
        cv2.circle(frame, (80, y), 12, (245, 245, 245), -1)
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
        video = tmp / "clip.mp4"
        cache = tmp / "cache"
        write_video(video)

        def run(**kwargs):
            kwargs.setdefault("cache_dir", str(cache))
            return g.track_roi_cached(str(video), ROI, **kwargs)

        start = time.monotonic()
        ts1, y1, size1, cuts1, st1 = run()
        cold = time.monotonic() - start

        start = time.monotonic()
        ts2, y2, size2, cuts2, st2 = run()
        warm = time.monotonic() - start

        check("Cache-Datei wurde angelegt", any(cache.glob("track-*.npz")))
        check("Zeitstempel identisch", np.array_equal(ts1, ts2))
        check("Positionskurve identisch", np.allclose(y1, y2, rtol=0, atol=0),
              f"max. Abweichung {np.max(np.abs(np.asarray(y1) - np.asarray(y2))) if len(y1) else 0}")
        check("Bildgröße identisch", size1 == size2, f"{size1} vs {size2}")
        check("Szenenschnitte identisch", list(cuts1) == list(cuts2))
        check("Trackingstatistik identisch", st1 == st2, f"{st1} vs {st2}")
        check("Verlustzähler ist enthalten", "tracker_lost_frames" in st1, str(st1))
        check("warmer Lauf ist deutlich schneller", warm < cold / 3,
              f"kalt {cold:.2f}s, warm {warm:.2f}s")

        # --- Schlüssel muss auf allen Trackingparametern beruhen -----------
        before = len(list(cache.glob("track-*.npz")))

        run(camera_compensation=False)
        check("andere Kamerakompensation -> eigener Cache-Eintrag",
              len(list(cache.glob("track-*.npz"))) == before + 1)

        before = len(list(cache.glob("track-*.npz")))
        run(scene_cut_detection=False)
        check("andere Szenenschnitt-Einstellung -> eigener Cache-Eintrag",
              len(list(cache.glob("track-*.npz"))) == before + 1)

        before = len(list(cache.glob("track-*.npz")))
        run(max_frames=20)
        check("anderes max_frames -> eigener Cache-Eintrag",
              len(list(cache.glob("track-*.npz"))) == before + 1)

        # Andere ROI: eigener Eintrag.
        before = len(list(cache.glob("track-*.npz")))
        g.track_roi_cached(str(video), (40, 20, 24, 24), cache_dir=str(cache))
        check("andere ROI -> eigener Cache-Eintrag",
              len(list(cache.glob("track-*.npz"))) == before + 1)

        # --- Cache abschaltbar --------------------------------------------
        before = len(list(cache.glob("track-*.npz")))
        g.track_roi_cached(str(video), (12, 10, 20, 20), cache_dir=None)
        check("cache_dir=None schreibt nichts",
              len(list(cache.glob("track-*.npz"))) == before)

        # --- Beschädigter Cache darf den Lauf nicht verhindern ------------
        victim = sorted(cache.glob("track-*.npz"))[0]
        victim.write_bytes(b"kaputt")
        try:
            g.track_roi_cached(str(video), ROI, cache_dir=str(cache))
            check("beschädigter Cache-Eintrag wird neu berechnet statt zu scheitern", True)
        except Exception as exc:
            check("beschädigter Cache-Eintrag wird neu berechnet statt zu scheitern",
                  False, str(exc))

        # --- Geändertes Video invalidiert den Schlüssel --------------------
        key_before = g._track_cache_key(str(video), ROI, None, True, True)
        time.sleep(1.1)  # mtime-Auflösung abwarten
        write_video(video)
        key_after = g._track_cache_key(str(video), ROI, None, True, True)
        check("geändertes Video -> anderer Schlüssel", key_before != key_after)

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
