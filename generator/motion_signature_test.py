"""Regressionstest für die Bewegungssignatur.

Abgrenzung, die den ganzen Ansatz bestimmt: Dieses Modul erkennt NICHT,
*was* zu sehen ist - das wäre Bedeutungserkennung und bräuchte ein
trainiertes Modell. Es erkennt, *dass eine Szene derselben Art ist wie eine
früher benannte*. Getestet wird entsprechend die Ähnlichkeit, nicht die
Benennung.

Zwei Eigenschaften entscheiden über die Brauchbarkeit:

  * Gleichartige Szenen müssen sich nahe sein, verschiedene fern. Ohne
    Abstand dazwischen gibt es keine Schwelle, die beides trennt.
  * Oberhalb der Schwelle darf NICHTS zugeordnet werden. Eine falsche
    Zuordnung überträgt Parameter, die nicht passen - das ist schlechter
    als gar keine Zuordnung, und es fällt später schwerer auf.

Dauer: rund zwei Minuten. Ausführen:
  python3 generator/motion_signature_test.py
"""

import sys
import tempfile
from pathlib import Path

import cv2
import numpy as np

import motion_signature as ms

W, H, FPS = 320, 240, 25


def textured_background(seed=5):
    rng = np.random.default_rng(seed)
    bg = np.full((H, W, 3), 40, np.uint8)
    for _ in range(80):
        x, y = int(rng.integers(0, W)), int(rng.integers(0, H))
        color = tuple(int(v) for v in rng.integers(60, 190, 3))
        cv2.rectangle(bg, (x, y), (x + int(rng.integers(8, 24)), y + int(rng.integers(8, 24))),
                      color, -1)
    return cv2.GaussianBlur(bg, (3, 3), 0)


def write(path, kind, seconds=10, freq=1.0):
    """Erzeugt Videos mit strukturell verschiedenen Bewegungsmustern."""
    bg = textured_background()
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    for i in range(FPS * seconds):
        t = i / FPS
        frame = bg.copy()
        if kind == "vertical":
            cv2.circle(frame, (160, int(120 + 55 * np.sin(2 * np.pi * freq * t))), 20,
                       (250, 250, 250), -1)
        elif kind == "horizontal":
            cv2.circle(frame, (int(160 + 60 * np.sin(2 * np.pi * freq * t)), 120), 20,
                       (250, 250, 250), -1)
        elif kind == "two_regions":
            cv2.circle(frame, (80, int(80 + 40 * np.sin(2 * np.pi * freq * t))), 16,
                       (250, 250, 250), -1)
            cv2.circle(frame, (240, int(160 - 40 * np.sin(2 * np.pi * freq * t))), 16,
                       (220, 220, 220), -1)
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
        write(tmp / "v1.mp4", "vertical", freq=1.0)
        write(tmp / "v2.mp4", "vertical", freq=1.4)   # gleiche Art, anderes Tempo
        write(tmp / "h.mp4", "horizontal", freq=1.0)
        write(tmp / "two.mp4", "two_regions", freq=1.0)

        sig_v1 = ms.extract(str(tmp / "v1.mp4"), max_seconds=10)
        sig_v2 = ms.extract(str(tmp / "v2.mp4"), max_seconds=10)
        sig_h = ms.extract(str(tmp / "h.mp4"), max_seconds=10)
        sig_two = ms.extract(str(tmp / "two.mp4"), max_seconds=10)

        check("Signatur enthält alle Felder",
              all(f in sig_v1 for f in ms.SIGNATURE_FIELDS), str(sorted(sig_v1)))
        check("alle Werte liegen zwischen 0 und 1",
              all(0.0 <= float(sig_v1[f]) <= 1.0 for f in ms.SIGNATURE_FIELDS),
              str(sig_v1))

        # Richtung muss das deutlichste Unterscheidungsmerkmal sein.
        check("senkrechte Bewegung wird als senkrecht erkannt",
              sig_v1["vertical_share"] > 0.6, f"{sig_v1['vertical_share']:.2f}")
        check("waagerechte Bewegung wird als waagerecht erkannt",
              sig_h["vertical_share"] < 0.4, f"{sig_h['vertical_share']:.2f}")

        # Ähnlichkeit
        same = ms.distance(sig_v1, sig_v2)
        different = ms.distance(sig_v1, sig_h)
        check("gleichartige Szenen liegen nahe beieinander", same < 0.15, f"{same:.3f}")
        check("verschiedene Szenen liegen weit auseinander", different > 0.2,
              f"{different:.3f}")
        check("der Abstand dazwischen ist groß genug für eine Schwelle",
              different > same * 2, f"gleich {same:.3f}, verschieden {different:.3f}")

        # Zuordnung
        known = [{"label": "senkrecht", "signature": sig_v1, "parameters": {"smooth": 11}}]
        match, distance = ms.find_similar(sig_v2, known)
        check("gleichartige Szene wird zugeordnet",
              match is not None and match["label"] == "senkrecht", f"{distance:.3f}")
        check("die gemerkten Parameter kommen mit",
              match and match["parameters"].get("smooth") == 11)

        match, distance = ms.find_similar(sig_h, known)
        check("fremdartige Szene wird NICHT zugeordnet", match is None, f"{distance:.3f}")

        match, _ = ms.find_similar(sig_v1, [])
        check("ohne gemerkte Szenen wird nichts zugeordnet", match is None)

        # Speichern und Laden
        store = str(tmp / "szenen.jsonl")
        ms.save_labelled(store, "senkrecht", sig_v1, {"smooth": 11})
        ms.save_labelled(store, "zwei Regionen", sig_two, {"roi2": "auto"})
        entries = ms.load_labelled(store)
        check("beide Einträge werden gespeichert und gelesen", len(entries) == 2,
              str(len(entries)))
        check("Benennung bleibt erhalten",
              {e["label"] for e in entries} == {"senkrecht", "zwei Regionen"})

        with open(store, "a", encoding="utf-8") as fh:
            fh.write('{"abgebrochen": tru\n')
        check("kaputte Zeile wird übersprungen", len(ms.load_labelled(store)) == 2)

        # Eintrag aus einer anderen Signaturfassung wäre stillschweigend
        # falsch zugeordnet.
        with open(store, "a", encoding="utf-8") as fh:
            fh.write('{"version": 99, "label": "alt", "signature": {}}\n')
        check("Eintrag aus anderer Fassung wird verworfen",
              all(e["label"] != "alt" for e in ms.load_labelled(store)))

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
