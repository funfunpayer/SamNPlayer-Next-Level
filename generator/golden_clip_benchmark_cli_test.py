"""Test für golden_clip_benchmark.py's run_clip()/run_benchmark() - den
Ende-zu-Ende-Pfad über einen echten Subprozessaufruf von
generate_funscript.py, mit einem per cv2 erzeugten synthetischen Clip.
Dasselbe Muster wie ai_quality_cli_test.py/audio_check_cli_test.py; die
reinen Funktionen (build_clip_args, summarize, ...) sind bereits in
golden_clip_benchmark_test.py abgedeckt, das in CI läuft - dieses hier
nicht, aus demselben Grund wie die anderen *_cli_test.py-Dateien.

Ausführen: python3 generator/golden_clip_benchmark_cli_test.py
"""

import json
import sys
import tempfile
from pathlib import Path

import cv2
import numpy as np

import golden_clip_benchmark as bench

W, H, FPS, FRAMES = 160, 120, 25, 75


def write_video(path):
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    rng = np.random.default_rng(7)
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


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory() as tmp:
        video = Path(tmp) / "scene.mp4"
        write_video(video)
        manifest_path = Path(tmp) / "manifest.json"
        history_path = Path(tmp) / "history.jsonl"
        manifest = {"clips": [
            {"name": "gut", "video": str(video), "roi": [56, 36, 48, 48]},
            {"name": "kaputt", "video": str(Path(tmp) / "fehlt.mp4"), "roi": [0, 0, 10, 10]},
        ]}
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

        result = bench.run_benchmark(str(manifest_path), report_progress=False)
        check("run_benchmark verarbeitet beide Clips", len(result["clips"]) == 2, str(result))
        good = next(r for r in result["clips"] if r["name"] == "gut")
        broken = next(r for r in result["clips"] if r["name"] == "kaputt")
        check("gültiger Clip liefert ok=True mit Quality-Score",
              good["ok"] and good["quality_score"] is not None, str(good))
        check("kaputter Clip liefert ok=False statt den Lauf abzubrechen",
              broken["ok"] is False and "error" in broken, str(broken))
        check("summary zählt genau einen erfolgreichen Clip",
              result["summary"]["ok"] == 1, str(result["summary"]))

        bench.append_history(history_path, result)
        history = bench.load_history(history_path)
        check("Verlauf enthält den gerade angehängten Lauf", len(history) == 1, str(history))

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
