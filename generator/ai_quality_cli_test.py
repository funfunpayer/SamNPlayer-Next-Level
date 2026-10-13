"""Test für die CLI-Verdrahtung von --ai-quality-opinion in
generate_funscript.py (der dritte und letzte Baustein aus
docs/AI_ADAPTER.md: eine KI-Zweitmeinung neben dem Quality Doctor, ohne
dessen passed/score zu verändern).

Läuft über einen echten Subprozessaufruf durch die volle Pipeline (wie
generator.go es in der GUI tatsächlich aufruft), aber ohne laufenden
Colibri-Server - genau der Alltagsfall, den die Verdrahtung sauber
behandeln muss. build_prompt()/parse_response() sind bereits in
ai_quality_test.py abgedeckt; hier geht es um die Einhängung in die echte
Pipeline und den Bericht.

Ausführen: python3 generator/ai_quality_cli_test.py
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

        # --- ohne --ai-quality-opinion: Bericht enthält ai_opinion=None ---------
        report1 = Path(tmp) / "report1.jsonl"
        out1 = Path(tmp) / "out1.funscript"
        r1 = run(*common, "--output", str(out1), "--report", str(report1))
        check("Lauf ohne --ai-quality-opinion beendet erfolgreich", r1.returncode == 0, r1.stderr)
        entry1 = json.loads(report1.read_text().splitlines()[-1])
        check("ai_opinion ist None, wenn die Option nicht gesetzt ist",
              entry1["quality"]["ai_opinion"] is None, str(entry1["quality"]))
        check("quality.passed/score bleiben die normalen Doctor-Werte",
              "passed" in entry1["quality"] and "score" in entry1["quality"], "")
        funscript1 = json.loads(out1.read_text())
        check("ai_opinion fehlt in der .funscript-Metadata, wenn es keins gibt "
              "(kein leerer/erfundener Eintrag)",
              "ai_opinion" not in funscript1["metadata"], str(funscript1["metadata"]))

        # --- mit --ai-quality-opinion, aber ohne laufenden Server ---------------
        report2 = Path(tmp) / "report2.jsonl"
        out2 = Path(tmp) / "out2.funscript"
        r2 = run(*common, "--output", str(out2), "--report", str(report2),
                 "--ai-quality-opinion", "--ai-base-url", "http://127.0.0.1:1")
        check("Lauf mit --ai-quality-opinion und totem Server beendet sauber "
              "(kein Absturz)", r2.returncode == 0, r2.stderr)
        check("meldet den nicht erreichbaren Server auf stderr",
              "Kein Colibri-Server" in r2.stderr, r2.stderr)
        entry2 = json.loads(report2.read_text().splitlines()[-1])
        check("ai_opinion bleibt None, statt einen Vorschlag zu erfinden",
              entry2["quality"]["ai_opinion"] is None, str(entry2["quality"]))
        funscript2 = json.loads(out2.read_text())
        check("auch mit --ai-quality-opinion fehlt ai_opinion in der Metadata, "
              "wenn kein Server erreichbar war",
              "ai_opinion" not in funscript2["metadata"], str(funscript2["metadata"]))
        check("quality.passed/score sind identisch zum Lauf ohne KI-Option "
              "(die Zweitmeinung verändert die Doctor-Bewertung nicht)",
              (entry1["quality"]["passed"], entry1["quality"]["score"]) ==
              (entry2["quality"]["passed"], entry2["quality"]["score"]),
              f"{entry1['quality']} vs {entry2['quality']}")

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
