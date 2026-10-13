"""Regressionstest für das Erscheinungsgedächtnis.

Es bildet die eine Fähigkeit klassisch nach, die ein Objekterkenner
praktisch liefert: "dieses Objekt ist jetzt hier, auch wenn es woanders
liegt als eben". Solange der Tracker sicher läuft, werden Bildausschnitte
der Region gemerkt; bei Verlust oder Szenenschnitt wird das ganze Bild
dagegen abgeglichen, statt blind an der letzten Position neu zu verankern.

Der Fehlerfall, den es behebt, ist gemessen: an einem Video mit drei Szenen,
in denen das Objekt jeweils woanders lag, klebte der Tracker ab dem ersten
Schnitt auf Hintergrund - Szene 1 hatte 90px Bewegungsumfang, Szene 2 und 3
nur noch 1,5 und 5,0px. Und er meldete dabei KEINEN Verlust, weil er den
falschen Ausschnitt zuverlässig verfolgte.

Ebenso wichtig ist die Gegenrichtung: unterhalb einer Mindestübereinstimmung
darf NICHT neu verankert werden. Eine geratene Position sieht aus wie eine
Messung, ist aber keine - und wäre schlechter als die alte.

Dauer: mehrere Minuten. Ausführen:
  python3 generator/appearance_memory_test.py
"""

import sys
import tempfile
from pathlib import Path

import cv2
import numpy as np

import generate_funscript as g

W, H, FPS, SCENE_SECONDS = 320, 240, 25, 8
SCENES = [
    dict(seed=1, cx=80, cy0=70, amp=45, freq=1.0),
    dict(seed=2, cx=240, cy0=160, amp=50, freq=0.7),
    dict(seed=3, cx=160, cy0=120, amp=40, freq=1.4),
]
START_ROI = (60, 50, 40, 40)


def background(seed):
    rng = np.random.default_rng(seed)
    bg = np.full((H, W, 3), int(rng.integers(25, 55)), np.uint8)
    for _ in range(70):
        x, y = int(rng.integers(0, W)), int(rng.integers(0, H))
        color = tuple(int(v) for v in rng.integers(60, 190, 3))
        cv2.rectangle(bg, (x, y), (x + int(rng.integers(8, 24)), y + int(rng.integers(8, 24))),
                      color, -1)
    return cv2.GaussianBlur(bg, (3, 3), 0)


def write_video(path):
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    for scene in SCENES:
        bg = background(scene["seed"])
        for i in range(FPS * SCENE_SECONDS):
            t = i / FPS
            frame = bg.copy()
            y = int(scene["cy0"] + scene["amp"] * np.sin(2 * np.pi * t * scene["freq"]))
            cv2.circle(frame, (scene["cx"], y), 20, (250, 250, 250), -1)
            vw.write(frame)
    vw.release()


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    # --- Einheiten des Gedächtnisses selbst -------------------------------
    memory = g.AppearanceMemory(max_templates=3)
    check("ohne Vorlagen wird nichts gefunden",
          memory.reacquire(np.zeros((H, W), np.uint8), START_ROI) is None)

    rng = np.random.default_rng(7)
    frame = rng.integers(0, 255, (H, W), dtype=np.uint8)
    memory.remember(frame, (50, 50, 40, 40))
    check("eine Vorlage wurde gemerkt", len(memory.templates) == 1)

    found = memory.reacquire(frame, (50, 50, 40, 40))
    check("dieselbe Region wird im selben Bild wiedergefunden", found is not None, str(found))
    if found:
        check("und zwar ungefähr an der richtigen Stelle",
              abs(found[0] - 50) < 15 and abs(found[1] - 50) < 15, str(found))

    # In einem völlig anderen Bild darf NICHTS gefunden werden.
    other = np.full((H, W), 128, np.uint8)
    check("in einem fremden Bild wird nicht geraten",
          memory.reacquire(other, (50, 50, 40, 40)) is None)
    check("der erfolglose Versuch wird gezählt", memory.failed_reacquisitions > 0)

    # Die Sammlung wächst nicht unbegrenzt, und der erste Ausschnitt bleibt.
    first = memory.templates[0]
    for i in range(10):
        memory.remember(frame, (10 + i * 5, 10, 40, 40))
    check("die Sammlung bleibt begrenzt", len(memory.templates) <= 3,
          str(len(memory.templates)))
    check("der allererste Ausschnitt bleibt erhalten",
          np.array_equal(memory.templates[0], first))

    # --- Wirkung auf ein echtes Video ------------------------------------
    with tempfile.TemporaryDirectory() as tmp:
        video = Path(tmp) / "three_scenes.mp4"
        write_video(video)

        _, without, _, _, stats_off = g.track_roi(
            str(video), START_ROI, camera_compensation=False, appearance_memory=False)
        _, with_memory, _, _, stats_on = g.track_roi(
            str(video), START_ROI, camera_compensation=False, appearance_memory=True)

        ranges = [(0, 200), (200, 400), (400, 600)]
        spans_off = [float(np.ptp(without[a:b])) for a, b in ranges if b <= len(without)]
        spans_on = [float(np.ptp(with_memory[a:b])) for a, b in ranges if b <= len(with_memory)]

        check("ohne Gedächtnis gehen die späteren Szenen verloren",
              spans_off[1] < 20 and spans_off[2] < 20,
              str([round(v, 1) for v in spans_off]))
        check("mit Gedächtnis hat jede Szene echte Bewegung",
              all(v > 50 for v in spans_on), str([round(v, 1) for v in spans_on]))
        check("die erste Szene bleibt unverändert",
              abs(spans_on[0] - spans_off[0]) < 5,
              f"{spans_on[0]:.1f} gegen {spans_off[0]:.1f}")
        check("Wiederfindungen werden gezählt",
              stats_on["reacquisitions"] >= 2, str(stats_on["reacquisitions"]))
        check("ohne Gedächtnis gibt es keine Wiederfindungen",
              stats_off["reacquisitions"] == 0)

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
