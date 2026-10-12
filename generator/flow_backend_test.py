"""Regressionstest für das Flow-Backend.

Prüft die Eigenschaften, auf denen der Entwurf beruht und die beim
Weiterentwickeln leicht kaputtgehen:

  * Die Amplitude muss stimmen. Der Schwerpunkt über das GESAMTE Flow-Feld
    wird vom Bildzentrum angezogen und lieferte nur 68 von 110px; deshalb
    wird über die stärksten Flow-Anteile gerechnet.

  * Es darf NICHT integriert werden. Mittleren Flow pro Frame aufzusummieren
    ergab an einem Video mit 110px echter Bewegung 1.2px, weil sich die
    Schätzfehler aufheben. Der Test hält fest, dass eine Position gemessen
    wird - erkennbar daran, dass das Ergebnis auch bei doppelter Videolänge
    nicht wegdriftet.

  * Die Uneinigkeit der Schätzer muss brauchbares von unbrauchbarem Material
    trennen. Gemessen: gültige Videos 1.6-2.4px, unbewegtes Objekt 4.7px,
    reines Rauschen 25.7px.

Dauer: rund zwei Minuten. Ausführen:
  python3 generator/flow_backend_test.py
"""

import sys
import tempfile
from pathlib import Path

import cv2
import numpy as np

import flow_backend

W, H, FPS = 320, 240, 25
TRUE_AMPLITUDE = 110.0


def textured_background(seed=11):
    rng = np.random.default_rng(seed)
    bg = np.full((H, W, 3), 40, np.uint8)
    for _ in range(90):
        x, y = int(rng.integers(0, W)), int(rng.integers(0, H))
        color = tuple(int(v) for v in rng.integers(60, 190, 3))
        cv2.rectangle(bg, (x, y), (x + int(rng.integers(8, 24)), y + int(rng.integers(8, 24))),
                      color, -1)
    return cv2.GaussianBlur(bg, (3, 3), 0)


def write_moving(path, seconds=12, freq=1.0):
    bg = textured_background()
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    for i in range(FPS * seconds):
        t = i / FPS
        frame = bg.copy()
        y = int(120 + (TRUE_AMPLITUDE / 2) * np.sin(2 * np.pi * freq * t))
        cv2.circle(frame, (160, y), 20, (250, 250, 250), -1)
        vw.write(frame)
    vw.release()


def write_moving_with_pan(path, seconds=12, freq=1.0):
    """Wie write_moving, aber mit schwenkender Kamera. Das Objekt liegt in
    WELTkoordinaten und wandert im Bild mit - zeichnete man es an fester
    Bildschirmposition, machte es den Schwenk nicht mit und die Kompensation
    sähe fälschlich schlecht aus."""
    bg = textured_background()
    big = cv2.copyMakeBorder(bg, 60, 60, 60, 60, cv2.BORDER_REFLECT)
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    for i in range(FPS * seconds):
        t = i / FPS
        off_x = int(60 + 40 * np.sin(2 * np.pi * t * 0.12))
        off_y = int(60 + 25 * np.sin(2 * np.pi * t * 0.09))
        frame = big[off_y:off_y + H, off_x:off_x + W].copy()
        world_y = 60 + 120 + (TRUE_AMPLITUDE / 2) * np.sin(2 * np.pi * freq * t)
        cv2.circle(frame, (160, int(world_y - off_y)), 20, (250, 250, 250), -1)
        vw.write(frame)
    vw.release()


def write_noise(path, seconds=10):
    rng = np.random.default_rng(3)
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    for _ in range(FPS * seconds):
        vw.write(rng.integers(0, 255, (H, W, 3), dtype=np.uint8))
    vw.release()


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)

        short = tmp / "moving_short.mp4"
        write_moving(short, seconds=8)
        ts, pos, size, cuts, stats = flow_backend.analyze(str(short))

        check("liefert dieselbe Rückgabeform wie track_roi",
              len(ts) == len(pos) and size == (W, H) and isinstance(stats, dict))
        check("Zeitstempel streng aufsteigend", bool(np.all(np.diff(ts) > 0)))
        check("Amplitude trifft die echte Bewegung",
              abs(np.ptp(pos) - TRUE_AMPLITUDE) < 25,
              f"{np.ptp(pos):.1f} statt {TRUE_AMPLITUDE}")
        check("Positionen bleiben im Bild",
              float(pos.min()) >= -5 and float(pos.max()) <= H + 5,
              f"{pos.min():.1f}..{pos.max():.1f}")

        # Kein Wegdriften bei längerem Video: das wäre das Kennzeichen einer
        # integrierten Geschwindigkeit statt einer gemessenen Position.
        long_video = tmp / "moving_long.mp4"
        write_moving(long_video, seconds=20)
        _, pos_long, _, _, stats_long = flow_backend.analyze(str(long_video))
        check("keine Drift bei längerem Video",
              abs(np.ptp(pos_long) - np.ptp(pos)) < 25,
              f"kurz {np.ptp(pos):.1f} vs lang {np.ptp(pos_long):.1f}")
        first_half = float(np.mean(pos_long[:len(pos_long) // 2]))
        second_half = float(np.mean(pos_long[len(pos_long) // 2:]))
        check("Mittelwert wandert nicht davon",
              abs(first_half - second_half) < 20, f"{first_half:.1f} vs {second_half:.1f}")

        # Uneinigkeit der Schätzer als Vertrauensmaß.
        noise = tmp / "noise.mp4"
        write_noise(noise)
        _, _, _, _, noise_stats = flow_backend.analyze(str(noise))
        check("Schätzer sind sich bei echter Bewegung einig",
              stats["center_disagreement"] < 5, str(stats["center_disagreement"]))
        check("Schätzer sind sich bei Rauschen uneinig",
              noise_stats["center_disagreement"] > stats["center_disagreement"] * 2,
              f"Rauschen {noise_stats['center_disagreement']} vs "
              f"Bewegung {stats['center_disagreement']}")

        # --- Kamerabewegung: Position gegen Position ----------------------
        # Die Korrektur arbeitet auf der POSITION (merkmalsbasiert, per
        # RANSAC), nicht auf den Flow-Vektoren. Das war der entscheidende
        # Unterschied: mit Vektorkorrektur 145.2px statt 110, mit
        # Positionskorrektur 100.2px.
        panning = tmp / "panning.mp4"
        write_moving_with_pan(panning, seconds=12)
        _, pos_pan, _, _, _ = flow_backend.analyze(str(panning))
        overshoot = float(np.ptp(pos_pan))
        check("Amplitude bei Schwenk in vertretbarem Rahmen",
              overshoot < TRUE_AMPLITUDE * 1.35,
              f"{overshoot:.1f} statt {TRUE_AMPLITUDE}")

        # Gegenprobe: ohne die merkmalsbasierte Korrektur muss es deutlich
        # schlechter sein. Sonst wirkt die Korrektur gar nicht und der Test
        # oben würde das nicht bemerken.
        _, pos_uncorrected, _, _, _ = flow_backend.analyze(str(panning),
                                                           camera_compensation=False)
        check("Kamerakorrektur verbessert die Amplitude messbar",
              abs(np.ptp(pos_uncorrected) - TRUE_AMPLITUDE) > abs(overshoot - TRUE_AMPLITUDE),
              f"ohne {np.ptp(pos_uncorrected):.1f}, mit {overshoot:.1f}")

        # Achsenwahl
        _, pos_x, _, _, _ = flow_backend.analyze(str(short), axis="x")
        check("waagerechte Achse liefert bei senkrechter Bewegung wenig Ausschlag",
              float(np.ptp(pos_x)) < float(np.ptp(pos)),
              f"x {np.ptp(pos_x):.1f} vs y {np.ptp(pos):.1f}")

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
