"""Test für die CLI-Verdrahtung von --label-scene / --suggest-profile in
generate_funscript.py (der dritte Baustein aus docs/AI_ADAPTER.md: das
klassische motion_signature.find_similar() entscheidet zuerst, eine KI wird
nur gefragt, wenn das keinen sicheren Treffer liefert).

Läuft über einen echten Subprozessaufruf des Skripts - so, wie
generator/generator.go es in der GUI tatsächlich aufruft (siehe FindROI
dort) - statt argparse.Namespace von Hand nachzubauen. decode/select-artige
reine Funktionen sind bereits in motion_signature_test.py und
ai_profile_test.py abgedeckt; hier geht es um das Zusammenspiel der beiden
über die CLI.

Ausführen: python3 generator/profile_suggestion_test.py
"""

import subprocess
import sys
import tempfile
import json
from pathlib import Path

import cv2
import numpy as np

SCRIPT = Path(__file__).parent / "generate_funscript.py"
W, H, FPS, FRAMES = 160, 120, 25, 60


def write_video(path):
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    rng = np.random.default_rng(3)
    bg = np.full((H, W, 3), 40, np.uint8)
    for _ in range(40):
        x, y = int(rng.integers(0, W)), int(rng.integers(0, H))
        cv2.rectangle(bg, (x, y), (x + 8, y + 8), (90, 90, 90), -1)
    for i in range(FRAMES):
        frame = bg.copy()
        y = int(60 + 40 * np.sin(2 * np.pi * i / FPS))
        cv2.circle(frame, (80, y), 12, (250, 250, 250), -1)
        vw.write(frame)
    vw.release()


def run(*args):
    result = subprocess.run(
        [sys.executable, str(SCRIPT), *args],
        capture_output=True, text=True, cwd=str(SCRIPT.parent), timeout=60)
    return result


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory() as tmp:
        video = Path(tmp) / "scene.mp4"
        write_video(video)
        sig_path = Path(tmp) / "signatures.jsonl"

        # --- --label-scene: speichert, ohne die Tracking-Pipeline zu laufen -----
        r1 = run("--video", str(video), "--label-scene", "meine-szene",
                 "--scene-profile", "standard", "--signature-path", str(sig_path))
        check("--label-scene beendet erfolgreich", r1.returncode == 0, r1.stderr)
        check("Signaturdatei wurde angelegt", sig_path.is_file(), str(sig_path))
        check("Bestätigung auf stderr", "meine-szene" in r1.stderr, r1.stderr)
        saved = json.loads(sig_path.read_text().splitlines()[0])
        check("bestätigtes Profil wird als Trainingsziel gespeichert",
              saved.get("parameters", {}).get("profile") == "standard", str(saved))

        # --- Go bridge: feature extraction can be requested as JSON ------------
        r0 = run("--video", str(video), "--dump-motion-signature")
        try:
            dumped = json.loads(r0.stdout)
        except json.JSONDecodeError:
            dumped = {}
        check("--dump-motion-signature beendet erfolgreich", r0.returncode == 0, r0.stderr)
        check("Signaturbrücke liefert die acht erklärbaren Merkmale",
              len(dumped) == 8 and "rhythm_strength" in dumped, r0.stdout)

        # --- --suggest-profile: findet die gerade gespeicherte Szene wieder -----
        # Dasselbe Video gegen seine eigene gespeicherte Signatur hat Abstand 0 -
        # weit unter der Schwelle 0.15, der klassische Pfad muss also genügen
        # und darf KEINEN KI-Aufruf versuchen (kein Colibri-Server im Test).
        r2 = run("--video", str(video), "--suggest-profile",
                 "--signature-path", str(sig_path))
        check("--suggest-profile beendet erfolgreich", r2.returncode == 0, r2.stderr)
        check("findet die eigene Szene wieder (klassisch, ohne KI)",
              "PROFILE_SUGGESTION meine-szene measured" in r2.stdout, r2.stdout)
        check("nennt es einen gemessenen, keinen KI-Vorschlag",
              "gemessen, keine KI" in r2.stderr, r2.stderr)
        check("versucht keinen KI-Aufruf, wenn der klassische Treffer sicher ist",
              "Colibri" not in r2.stderr, r2.stderr)

        # --- ohne gespeicherte Szenen: fällt auf den KI-Pfad zurück und meldet --
        # den fehlenden Server ehrlich (kein Colibri in dieser Testumgebung),
        # statt so zu tun, als gäbe es einen Vorschlag.
        empty_sig_path = Path(tmp) / "empty.jsonl"
        r3 = run("--video", str(video), "--suggest-profile",
                 "--signature-path", str(empty_sig_path),
                 "--ai-base-url", "http://127.0.0.1:1")
        check("--suggest-profile ohne Treffer und ohne Server beendet sauber",
              r3.returncode == 0, r3.stderr)
        check("PROFILE_SUGGESTION wird NICHT ausgegeben, wenn nichts sicher ist",
              "PROFILE_SUGGESTION" not in r3.stdout, r3.stdout)
        check("meldet den nicht erreichbaren Server statt zu schweigen",
              "No Colibri server" in r3.stderr or "Kein Colibri-Server" in r3.stderr,
              r3.stderr)

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
