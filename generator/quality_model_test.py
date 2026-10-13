"""Regressionstest für die lernende Qualitätsbewertung.

Die Sicherungen sind hier wichtiger als die Lernfähigkeit selbst. Ein Modell,
das aus fünf Beispielen entsteht oder das schlechter ist als die Regeln, die
es ersetzt, wäre ein Rückschritt mit dem Anschein von Fortschritt - und
deutlich schwerer zu bemerken als eine schlechte Regel, weil es fundiert
aussieht.

Geprüft wird daher vor allem, wann NICHT gelernt wird:
  * zu wenige beurteilte Läufe
  * eine Klasse fehlt oder ist zu dünn besetzt
  * das gelernte Modell schlägt die Regeln nicht

Ausführen:  python3 generator/quality_model_test.py
"""

import json
import sys
import tempfile
from pathlib import Path

import numpy as np

import quality_model


def record(concentration, motion, lost, verdict, rule_passed=None):
    """Ein Lauf mit Kennzahlen und Urteil, wie er im Bericht steht."""
    if rule_passed is None:
        rule_passed = verdict == "brauchbar"
    return {
        "video_name": f"v{concentration:.2f}.mp4",
        "quality": {
            "score": 1.0 if rule_passed else 0.3,
            "passed": rule_passed,
            "warnings": [],
            "metrics": {
                "concentration": concentration,
                "motion_range_fraction": motion,
                "tracker_lost_fraction": lost,
                "actions_per_minute": 120.0,
                "gap_fraction": 0.0,
                "speed_spikes": 0,
                "action_count": 200,
            },
        },
        "feedback": {"verdict": verdict, "comment": "", "at": "2026-01-01T00:00:00+00:00"},
    }


def good_set(n=10):
    rng = np.random.default_rng(1)
    return [record(0.6 + rng.random() * 0.2, 0.4 + rng.random() * 0.1,
                   rng.random() * 0.05, "brauchbar") for _ in range(n)]


def bad_set(n=10):
    rng = np.random.default_rng(2)
    return [record(0.05 + rng.random() * 0.1, 0.02 + rng.random() * 0.03,
                   0.5 + rng.random() * 0.3, "unbrauchbar") for _ in range(n)]


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory() as tmp:
        model_path = str(Path(tmp) / "modell.json")

        # --- Zu wenige Beispiele -------------------------------------------
        ok, text = quality_model.train(good_set(3) + bad_set(3), model_path)
        check("aus zu wenigen Läufen wird nicht gelernt", not ok, text)
        check("Meldung nennt die Mindestzahl", str(quality_model.MIN_SAMPLES) in text, text)
        check("es wird keine Datei angelegt", not Path(model_path).exists())

        # --- Nur eine Klasse ------------------------------------------------
        ok, text = quality_model.train(good_set(20), model_path)
        check("aus einseitigen Urteilen wird nicht gelernt", not ok, text)

        # --- Regeln sind bereits perfekt -> Modell darf nicht übernehmen ---
        perfect = good_set(8) + bad_set(8)
        baseline = quality_model.rule_based_accuracy(perfect)
        check("Messlatte erkannt: Regeln treffen hier 100%", baseline == 1.0, str(baseline))
        ok, text = quality_model.train(perfect, model_path)
        check("Modell wird nicht übernommen, wenn es die Regeln nicht schlägt",
              not ok, text)
        check("Begründung wird genannt", "nicht besser" in text, text)

        # --- Regeln liegen daneben -> Modell soll übernehmen ---------------
        # Hier sind die Regeln bei den schlechten Läufen falsch: sie sagen
        # "bestanden", der Anwender sagt "unbrauchbar". Genau der Fall, für
        # den das Lernen gedacht ist.
        misjudged = good_set(8) + [
            record(0.05 + i * 0.01, 0.02, 0.6, "unbrauchbar", rule_passed=True)
            for i in range(8)
        ]
        check("Messlatte: Regeln treffen hier nur die Hälfte",
              abs(quality_model.rule_based_accuracy(misjudged) - 0.5) < 0.01,
              str(quality_model.rule_based_accuracy(misjudged)))
        ok, text = quality_model.train(misjudged, model_path)
        check("Modell wird übernommen, wenn es die Regeln schlägt", ok, text)
        check("Datei wurde geschrieben", Path(model_path).exists())

        # --- Geladenes Modell urteilt sinnvoll ------------------------------
        model = quality_model.load(model_path)
        check("Modell lässt sich laden", model is not None)
        good_p = quality_model.score(model, good_set(1)[0]["quality"]["metrics"])
        bad_p = quality_model.score(model, bad_set(1)[0]["quality"]["metrics"])
        check("gutes Ergebnis bekommt hohe Wahrscheinlichkeit", good_p > 0.5, f"{good_p:.2f}")
        check("schlechtes Ergebnis bekommt niedrige", bad_p < 0.5, f"{bad_p:.2f}")

        check("Beschreibung nennt die Trefferquote",
              "Trefferquote" in quality_model.describe(model))

        # --- Modell aus einer anderen Kennzahlreihenfolge wird verworfen ---
        raw = json.loads(Path(model_path).read_text())
        raw["features"] = ["ganz", "andere", "kennzahlen"]
        Path(model_path).write_text(json.dumps(raw))
        check("Modell mit anderen Kennzahlen wird verworfen",
              quality_model.load(model_path) is None)

        # --- Beschädigte Datei darf nicht abstürzen -------------------------
        Path(model_path).write_text("{kaputt")
        check("beschädigte Modelldatei wird verworfen statt zu scheitern",
              quality_model.load(model_path) is None)

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
