"""Test für die CLI-Verdrahtung von --audio-check in generate_funscript.py.

Läuft über einen echten Subprozessaufruf durch die volle Pipeline (wie
generator.go es in der GUI tatsächlich aufruft). cv2.VideoWriter erzeugte
Clips haben KEINE Audiospur - das ist hier gewollt: es prüft genau den
Alltagsfall "Video ohne brauchbare Tonspur", den die Verdrahtung sauber
behandeln muss (ehrliches "nicht möglich" statt Absturz oder erfundener
Werte). Die Tempo-Schätzung/der Vergleich selbst sind bereits in
audio_check_test.py an synthetischen Signalen abgedeckt.

Ausführen: python3 generator/audio_check_cli_test.py
"""

import json
import subprocess
import sys
import tempfile
from pathlib import Path

import cv2
import numpy as np

SCRIPT = Path(__file__).parent / "generate_funscript.py"
W, H, FPS, FRAMES = 160, 120, 25, 75


def write_video(path):
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    rng = np.random.default_rng(4)
    bg = np.full((H, W, 3), 40, np.uint8)
    for _ in range(40):
        x, y = int(rng.integers(0, W)), int(rng.integers(0, H))
        cv2.rectangle(bg, (x, y), (x + 8, y + 8), (90, 90, 90), -1)
    for i in range(FRAMES):
        frame = bg.copy()
        y = int(60 + 35 * np.sin(2 * np.pi * i / (FPS / 1.5)))
        cv2.circle(frame, (80, y), 14, (250, 250, 250), -1)
        vw.write(frame)
    vw.release()


def run(*args):
    return subprocess.run(
        [sys.executable, str(SCRIPT), *args],
        capture_output=True, text=True, cwd=str(SCRIPT.parent), timeout=120)


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory() as tmp:
        video = Path(tmp) / "scene.mp4"
        write_video(video)
        common = ["--video", str(video), "--roi", "56,36,48,48",
                  "--no-camera-compensation", "--no-scene-cut-detection"]

        # --- ohne --audio-check: Bericht enthält audio_check=None ---------------
        report1 = Path(tmp) / "report1.jsonl"
        out1 = Path(tmp) / "out1.funscript"
        r1 = run(*common, "--output", str(out1), "--report", str(report1))
        check("Lauf ohne --audio-check beendet erfolgreich", r1.returncode == 0, r1.stderr)
        entry1 = json.loads(report1.read_text().splitlines()[-1])
        check("audio_check ist None, wenn die Option nicht gesetzt ist",
              entry1["quality"]["audio_check"] is None, str(entry1["quality"]))

        # --- mit --audio-check, Video ohne Audiospur (cv2.VideoWriter) ----------
        report2 = Path(tmp) / "report2.jsonl"
        out2 = Path(tmp) / "out2.funscript"
        r2 = run(*common, "--output", str(out2), "--report", str(report2), "--audio-check")
        check("Lauf mit --audio-check auf einem Video ohne Audiospur beendet "
              "sauber (kein Absturz)", r2.returncode == 0, r2.stderr)
        check("meldet auf stderr, dass die Prüfung nicht möglich war",
              "nicht möglich" in r2.stderr, r2.stderr)
        entry2 = json.loads(report2.read_text().splitlines()[-1])
        check("audio_check.available ist False, wenn das Video keine Audiospur hat",
              entry2["quality"]["audio_check"]["available"] is False, str(entry2["quality"]))
        check("quality.passed/score sind identisch zum Lauf ohne die Option "
              "(die Prüfung verändert die Doctor-Bewertung nicht)",
              (entry1["quality"]["passed"], entry1["quality"]["score"]) ==
              (entry2["quality"]["passed"], entry2["quality"]["score"]),
              f"{entry1['quality']} vs {entry2['quality']}")
        funscript2 = json.loads(out2.read_text())
        check("audio_check fehlt in der .funscript-Metadata, wenn die Prüfung "
              "nicht möglich war (kein leerer/erfundener Eintrag, wie bei ai_opinion)",
              "audio_check" not in funscript2["metadata"], str(funscript2["metadata"]))

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
