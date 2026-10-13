"""Regressionstest für die Zusammenführung mehrerer Messquellen.

Das Modul ist bewusst NICHT im Erzeugungspfad verdrahtet - mit den heute
vorhandenen zwei Quellen bringt die Fusion messbar nichts (siehe Kommentar
in fusion.py). Getestet wird es trotzdem, damit es beim Hinzufügen einer
dritten Quelle sofort einsatzbereit ist und nicht erst repariert werden
muss.

Der Kern ist der Abgleich über ZEITSTEMPEL statt über Index. Quellen mit
unterschiedlichen Raten Index gegen Index zu addieren verschiebt die
Zeitachse stillschweigend - der Fehler wächst mit der Laufzeit und fällt
am Anfang eines Videos gar nicht auf.

Ausführen:  python3 generator/fusion_test.py
"""

import sys

import numpy as np

import fusion


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    # --- Abgleich über Zeitstempel, nicht über Index ----------------------
    # Zwei Quellen, dieselbe Bewegung, aber unterschiedliche Abtastraten.
    # Über Index zusammengeführt liefe die zweite Quelle zeitlich davon.
    t_fast = np.arange(0, 10000, 20.0)
    t_slow = np.arange(0, 10000, 50.0)
    signal = lambda t: (np.sin(2 * np.pi * t / 1000.0) + 1) * 50

    result = fusion.fuse([
        ("schnell", t_fast, signal(t_fast)),
        ("langsam", t_slow, signal(t_slow)),
    ])
    truth = signal(result["at"])
    correlation = float(np.corrcoef(result["pos"], truth)[0, 1])
    check("unterschiedliche Raten werden korrekt ausgerichtet",
          correlation > 0.99, f"{correlation:.3f}")

    check("Zeitraster deckt nur den gemeinsamen Bereich ab",
          result["at"][0] >= max(t_fast[0], t_slow[0])
          and result["at"][-1] <= min(t_fast[-1], t_slow[-1]))

    # --- Eine ausgerissene Quelle darf das Ergebnis nicht bestimmen -------
    rng = np.random.default_rng(4)
    broken = signal(t_fast).copy()
    broken[200:400] = rng.normal(50, 30, 200)   # 4 Sekunden Unsinn
    result = fusion.fuse([
        ("gut1", t_fast, signal(t_fast)),
        ("gut2", t_slow, signal(t_slow)),
        ("kaputt", t_fast, broken),
    ])
    weights = {n: float(np.mean(c["weight"])) for n, c in result["components"].items()}
    check("die kaputte Quelle bekommt das geringste Gewicht",
          weights["kaputt"] < weights["gut1"] and weights["kaputt"] < weights["gut2"],
          str({k: round(v, 3) for k, v in weights.items()}))

    truth = signal(result["at"])
    check("Ergebnis folgt trotz kaputter Quelle der Wahrheit",
          float(np.corrcoef(result["pos"], truth)[0, 1]) > 0.95,
          f"{float(np.corrcoef(result['pos'], truth)[0, 1]):.3f}")

    # --- Confidence ist aufgeschlüsselt -----------------------------------
    # Ohne die Einzelanteile ließe sich nicht beantworten, warum die Engine
    # einem Sample geglaubt hat - genau das ist der Zweck.
    parts = result["components"]["kaputt"]
    for key in ("base", "temporal_consistency", "cross_source_agreement", "weight"):
        check(f"Confidence-Anteil '{key}' vorhanden", key in parts)
    check("zeitliche Konsistenz bricht im kaputten Abschnitt ein",
          float(np.mean(parts["temporal_consistency"][200:400]))
          < float(np.mean(parts["temporal_consistency"][:200])),
          f"{np.mean(parts['temporal_consistency'][200:400]):.2f} gegen "
          f"{np.mean(parts['temporal_consistency'][:200]):.2f}")

    # --- Grundgüte wirkt --------------------------------------------------
    result_weighted = fusion.fuse(
        [("a", t_fast, signal(t_fast)), ("b", t_fast, signal(t_fast))],
        base_confidence={"a": 1.0, "b": 0.1})
    weights = {n: float(np.mean(c["weight"])) for n, c in result_weighted["components"].items()}
    check("quellenweite Grundgüte verschiebt die Gewichte",
          weights["a"] > weights["b"] * 5, str({k: round(v, 3) for k, v in weights.items()}))

    # --- Randfälle --------------------------------------------------------
    try:
        fusion.fuse([("a", np.array([0.0, 100.0]), np.array([0.0, 100.0])),
                     ("b", np.array([500.0, 600.0]), np.array([0.0, 100.0]))])
        check("ohne gemeinsamen Zeitbereich wird abgelehnt", False)
    except ValueError:
        check("ohne gemeinsamen Zeitbereich wird abgelehnt", True)

    single = fusion.fuse([("nur_eine", t_fast, signal(t_fast))])
    check("eine einzelne Quelle funktioniert ebenfalls",
          float(np.corrcoef(single["pos"], signal(single["at"]))[0, 1]) > 0.99)

    flat = fusion.fuse([("flach", t_fast, np.full(len(t_fast), 42.0)),
                        ("gut", t_fast, signal(t_fast))])
    check("konstante Quelle führt nicht zu Division durch Null",
          bool(np.all(np.isfinite(flat["pos"]))))

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
