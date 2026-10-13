"""Regressionstest für die szenenweise Regionssuche.

Baut ein Video mit drei Szenen, in denen sich das Objekt jeweils an einer
ANDEREN Bildstelle bewegt - der Normalfall bei geschnittenem Material und
genau der Fall, an dem die frühere Verarbeitung scheiterte.

Warum das so gefährlich war: nach einem Schnitt verankerte der Tracker an
der zuletzt bekannten Position neu. Lag das Objekt dort nicht mehr, verfolgte
CSRT ab da einen Hintergrundfleck - und meldete dabei NULL verlorene Frames,
weil es diesen falschen Fleck völlig zuverlässig verfolgte. Gemessen: Szene 1
hatte 90px Bewegungsumfang, Szene 2 und 3 nur noch 1.5px, und der Quality
Doctor gab dem Ergebnis 1.00 (bestanden).

Dauer: mehrere Minuten (drei Durchläufe über das Video). Ausführen:
  python3 generator/per_scene_roi_test.py
"""

import sys
import tempfile
from pathlib import Path

import cv2
import numpy as np

import generate_funscript as g
import quality_doctor

W, H, FPS, SCENE_SECONDS = 320, 240, 25, 8
SCENES = [
    dict(seed=1, cx=80, cy0=70, amp=45, freq=1.0),
    dict(seed=2, cx=240, cy0=160, amp=50, freq=0.7),
    dict(seed=3, cx=160, cy0=120, amp=40, freq=1.4),
]


def background(seed):
    rng = np.random.default_rng(seed)
    bg = np.full((H, W, 3), int(rng.integers(25, 55)), np.uint8)
    for _ in range(70):
        x, y = int(rng.integers(0, W)), int(rng.integers(0, H))
        color = tuple(int(v) for v in rng.integers(60, 190, 3))
        cv2.rectangle(bg, (x, y), (x + int(rng.integers(8, 24)), y + int(rng.integers(8, 24))), color, -1)
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

    with tempfile.TemporaryDirectory() as tmp:
        video = Path(tmp) / "three_scenes.mp4"
        write_video(video)
        expected_cuts = [FPS * SCENE_SECONDS, FPS * SCENE_SECONDS * 2]

        # --- Szenenerkennung ------------------------------------------------
        # Der zweite Schnitt ist der interessante: beide Szenen haben eine
        # ähnliche Helligkeitsverteilung, die Histogramm-Korrelation lag dort
        # bei 0.998. Nur das räumliche Signal findet ihn.
        scenes = g.detect_scene_boundaries(str(video))
        check("alle drei Szenen erkannt", len(scenes) == 3, str(scenes))
        if len(scenes) == 3:
            starts = [s[0] for s in scenes[1:]]
            check("Schnitte an der richtigen Stelle",
                  all(abs(a - b) <= 2 for a, b in zip(starts, expected_cuts)),
                  f"{starts} statt {expected_cuts}")

        roi_scene1 = (SCENES[0]["cx"] - 20, SCENES[0]["cy0"] - 20, 40, 40)

        # --- Altes Verhalten: eine Region fürs ganze Video ------------------
        _, y_old, _, _, stats_old = g.track_roi(
            str(video), roi_scene1, camera_compensation=False)
        old_spans = [float(np.ptp(y_old[a:b])) for a, b in
                     [(0, 200), (200, 400), (400, 600)] if b <= len(y_old)]
        # Früher (OpenCV/CSRT) verloren spätere Szenen die Bewegung (~1.5px),
        # während lost_frames=0 blieb - genau deshalb reicht der Zähler nicht.
        # Aktuelle OpenCV-Builds reproduzieren den Verlust nicht immer; die
        # positive Prüfung unten (track_by_scenes) bleibt die harte Anforderung.
        lost_later = (len(old_spans) == 3 and old_spans[1] < 20 and old_spans[2] < 20)
        print(("  OK   " if lost_later else "  INFO ") +
              "ohne szenenweise Suche: spätere Szenen " +
              ("verlieren Bewegung (wie früher gemessen)" if lost_later
               else f"behalten Bewegung (OpenCV weicht ab) "
                    f"Spannweiten {[round(v, 1) for v in old_spans]}"))
        check("und meldet dabei keinen Objektverlust (deshalb reicht der Zähler nicht)",
              stats_old["tracker_lost_frames"] == 0, str(stats_old["tracker_lost_frames"]))

        # --- Neues Verhalten ------------------------------------------------
        ts, y_new, _, _, stats_new, ranges = g.track_by_scenes(
            str(video), roi_scene1, camera_compensation=False)
        check("drei Szenenbereiche zurückgegeben", len(ranges) == 3, str(ranges))
        new_spans = [float(np.ptp(y_new[a:b])) for a, b in ranges]
        check("jede Szene hat jetzt echte Bewegung",
              all(v > 50 for v in new_spans), f"Spannweiten {[round(v, 1) for v in new_spans]}")
        check("Zeitstempel streng aufsteigend (keine Dopplung an Szenengrenzen)",
              bool(np.all(np.diff(ts) > 0)), f"{int(np.sum(np.diff(ts) <= 0))} Verstöße")

        # --- Normalisierung pro Szene --------------------------------------
        actions, dense = g.positions_to_funscript(ts, y_new, scene_ranges=ranges)
        for i, (a, b) in enumerate(ranges):
            span = float(np.ptp(dense["pos"][a:b]))
            check(f"Szene {i + 1} nutzt den vollen Wertebereich", span > 90, f"{span:.1f}")

        # --- Quality Doctor darf keinen Fehlalarm auslösen ------------------
        # Drei Szenen mit 1.0, 0.7 und 1.4 Hz ergeben global ein breites
        # Spektrum, obwohl jede Szene für sich sauber rhythmisch ist.
        result = quality_doctor.evaluate(
            actions, video_duration_ms=int(ts[-1]), dense_signal=dense,
            tracker_lost_fraction=stats_new["tracker_lost_frames"] / max(1, stats_new["total_frames"]),
            scene_ranges=ranges)
        check("korrektes Mehrszenen-Ergebnis besteht die Qualitätsprüfung",
              result["passed"], f"score={result['score']} {result['warnings']}")
        check("kein Rausch-Fehlalarm bei mehreren Rhythmen",
              not any("verrauscht" in w for w in result["warnings"]), str(result["warnings"]))

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
