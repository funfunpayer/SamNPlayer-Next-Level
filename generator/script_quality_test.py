"""Test für die CLI-Verdrahtung von --script-quality in generate_funscript.py
("Script Doctor" für importierte .funscript-Dateien, docs/NEXT.md "Later").

Anders als das normale Quality Doctor (läuft direkt nach einer Generierung,
kennt Tracking-Rohdaten wie den ungekürzten Kurvenverlauf und den
Tracker-Verlust-Anteil) arbeitet dieser Weg auf einer BEREITS VORHANDENEN
Datei ohne Video - z.B. eine aus einem anderen Werkzeug importierte. Ruft
quality_doctor.evaluate() mit nur actions + video_duration_ms auf (die
Actions-only-Prüfungen, siehe dessen eigene Docstring) und markiert das
Ergebnis mit estimatedFromScriptOnly, damit die Oberfläche es nicht mit
einer echten Post-Generierungs-Bewertung verwechselt.

Läuft über einen echten Subprozessaufruf, wie generator.go es später von
Go aus tun wird (siehe SuggestProfile dort für dasselbe Muster).

Ausführen: python3 generator/script_quality_test.py
"""

import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPT = Path(__file__).parent / "generate_funscript.py"


def run(*args):
    return subprocess.run(
        [sys.executable, str(SCRIPT), *args],
        capture_output=True, text=True, cwd=str(SCRIPT.parent), timeout=30)


def parse_result(stdout):
    m = re.search(r"^SCRIPT_QUALITY (.+)$", stdout, re.MULTILINE)
    return json.loads(m.group(1)) if m else None


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory() as tmp:
        # Sauberes, rhythmisches Skript - sollte durchgehen.
        clean_path = Path(tmp) / "clean.funscript"
        clean_actions = [{"at": i * 300, "pos": 20 if i % 2 == 0 else 80} for i in range(40)]
        clean_path.write_text(json.dumps({"actions": clean_actions}))

        result = run("--script-quality", str(clean_path))
        check("sauberes Skript: Exit-Code 0", result.returncode == 0, result.stderr)
        parsed = parse_result(result.stdout)
        check("SCRIPT_QUALITY-Zeile auf stdout gefunden", parsed is not None, result.stdout)
        if parsed:
            check("Ergebnis als 'nur aus dem Skript geschätzt' markiert",
                  parsed.get("estimatedFromScriptOnly") is True, str(parsed))
            check("Ergebnis hat score/warnings/passed (derselbe Vertrag wie evaluate())",
                  {"score", "warnings", "passed"} <= parsed.keys(), str(parsed))

        # Skript mit außerhalb 0-100 liegenden Werten - muss eine Warnung tragen.
        bad_path = Path(tmp) / "bad.funscript"
        bad_actions = [{"at": i * 300, "pos": 150 if i % 3 == 0 else 20} for i in range(20)]
        bad_path.write_text(json.dumps({"actions": bad_actions}))
        result = run("--script-quality", str(bad_path))
        parsed = parse_result(result.stdout)
        check("Werte außerhalb 0-100 werden auch ohne Video erkannt (actions-only-Check)",
              parsed is not None and any("außerhalb" in w for w in parsed.get("warnings", [])),
              str(parsed))

        # Nicht existierende Datei - klarer Fehler statt Absturz/leerer Ausgabe.
        result = run("--script-quality", str(Path(tmp) / "missing.funscript"))
        check("fehlende Datei: Exit-Code != 0", result.returncode != 0)
        check("fehlende Datei: verständliche Fehlermeldung auf stderr",
              "konnte nicht gelesen werden" in result.stderr, result.stderr)

        # Datei ohne 'actions'-Feld - derselbe klare Fehler, kein Absturz.
        empty_path = Path(tmp) / "empty.funscript"
        empty_path.write_text(json.dumps({"metadata": {}}))
        result = run("--script-quality", str(empty_path))
        check("Datei ohne actions: Exit-Code != 0", result.returncode != 0)

        # --script-quality braucht kein --video - muss NICHT auf den
        # generellen "--video ist erforderlich"-Fehler laufen.
        check("--script-quality braucht kein --video",
              "--video ist erforderlich" not in result.stderr, result.stderr)

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
