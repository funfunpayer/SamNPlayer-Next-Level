"""Regressionstest für Messbericht, Rückmeldung und Stapelverarbeitung.

Der Bericht existiert aus einem bestimmten Grund: sämtliche Schwellen im
Quality Doctor sind an synthetischen Testvideos bestimmt worden, deren
Wahrheit per Konstruktion bekannt war. Echtes Material ist unregelmäßiger und
liegt systematisch niedriger. Zum Nachziehen braucht es die Kennzahlen echter
Läufe UND ein menschliches Urteil dazu - die Zahlen allein sagen nicht, welche
zu einem brauchbaren Ergebnis gehören.

Zwei Eigenschaften sind kritisch und werden hier festgehalten:

  * Der Bericht muss Abbrüche überstehen. Deshalb JSON Lines mit Anhängen
    statt einer großen JSON-Datei, die bei jedem Schreiben neu erzeugt werden
    müsste, und deshalb überspringt der Leser kaputte Zeilen, statt zu
    scheitern.

  * Ein Urteil darf nur den passenden Lauf treffen, und bei mehreren Läufen
    zum selben Skript den jüngsten ohne Urteil.

Ausführen:  python3 generator/report_test.py
"""

import json
import sys
import tempfile
from pathlib import Path

import generate_funscript as g


def record(output, score=1.0, passed=True, concentration=0.7, motion=0.45):
    return {
        "video": f"/videos/{Path(output).stem}.mp4",
        "video_name": f"{Path(output).stem}.mp4",
        "output": output,
        "quality": {
            "score": score, "passed": passed, "warnings": [],
            "metrics": {"concentration": concentration,
                        "motion_range_fraction": motion,
                        "tracker_lost_fraction": 0.0,
                        "actions_per_minute": 120.0},
        },
        "feedback": None,
    }


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory() as tmp:
        report = str(Path(tmp) / "messwerte.jsonl")

        # --- Anhängen, nicht überschreiben --------------------------------
        g.write_report(report, record("/out/a.funscript"))
        g.write_report(report, record("/out/b.funscript", score=0.4, passed=False,
                                      concentration=0.1, motion=0.02))
        check("beide Läufe stehen im Bericht", len(g.read_report(report)) == 2)

        # --- Verzeichnis wird angelegt ------------------------------------
        nested = str(Path(tmp) / "neu" / "unterordner" / "m.jsonl")
        g.write_report(nested, record("/out/c.funscript"))
        check("fehlendes Verzeichnis wird angelegt", Path(nested).exists())

        # --- Kaputte Zeile darf den Bericht nicht unlesbar machen ---------
        with open(report, "a", encoding="utf-8") as fh:
            fh.write('{"abgebrochen": tru\n')
        check("kaputte Zeile wird übersprungen", len(g.read_report(report)) == 2,
              str(len(g.read_report(report))))

        # --- Urteil eintragen ---------------------------------------------
        check("Urteil trifft den richtigen Lauf",
              g.add_feedback(report, "/out/a.funscript", "brauchbar", "sauber") == 1)
        records = {r["output"]: r for r in g.read_report(report)}
        check("Urteil steht beim richtigen Skript",
              records["/out/a.funscript"]["feedback"]["verdict"] == "brauchbar")
        check("Kommentar wird gespeichert",
              records["/out/a.funscript"]["feedback"]["comment"] == "sauber")
        check("anderer Lauf bleibt unberührt",
              records["/out/b.funscript"]["feedback"] is None)

        check("unbekanntes Skript ergibt kein Urteil",
              g.add_feedback(report, "/out/gibtsnicht.funscript", "brauchbar") == 0)
        check("bereits beurteilter Lauf wird nicht doppelt beurteilt",
              g.add_feedback(report, "/out/a.funscript", "unbrauchbar") == 0)

        try:
            g.add_feedback(report, "/out/b.funscript", "vielleicht")
            check("ungültiges Urteil wird abgelehnt", False)
        except ValueError:
            check("ungültiges Urteil wird abgelehnt", True)

        # --- Jüngster passender Lauf bekommt das Urteil -------------------
        g.write_report(report, record("/out/b.funscript", score=0.9))
        g.add_feedback(report, "/out/b.funscript", "grenzwertig")
        b_records = [r for r in g.read_report(report) if r["output"] == "/out/b.funscript"]
        check("bei mehreren Läufen bekommt der jüngste das Urteil",
              b_records[-1]["feedback"] is not None and b_records[0]["feedback"] is None,
              str([bool(r["feedback"]) for r in b_records]))

        # --- Auswertung ----------------------------------------------------
        summary = g.summarize_report(report)
        check("Auswertung nennt beide Urteile",
              "brauchbar" in summary and "grenzwertig" in summary, summary)
        check("Auswertung zeigt die Kennzahlen",
              "concentration" in summary and "motion_range_fraction" in summary)
        check("Auswertung zählt Läufe ohne Urteil",
              "mit Urteil" in summary, summary.splitlines()[0])

        empty = g.summarize_report(str(Path(tmp) / "leer.jsonl"))
        check("leerer Bericht ergibt eine verständliche Meldung",
              "0 Läufe" in empty, empty)

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
