"""Test für fungen_compare.py - Regressionstest gegen die vier
Mess-Fehler, die in _compare_fungen.py (lokales Skript des Nutzers,
nicht Teil dieses Repos) gefunden wurden, siehe Moduldoku und
docs/FUNGEN_PARITY_PLAN.md ("Regression tests must expose the old
comparison errors: duplicate inputs, independent start-time shifting,
invalid correlation for constants, and ambiguous filename matching").

Fixtures folgen genau der im Plan geforderten Liste: bekannte Phase,
unregelmäßige Zyklen, Pausen, Amplitudenwechsel, verschobene Zeitstempel,
konstante Signale.

Ausführen: python3 generator/fungen_compare_test.py
"""

import json
import sys
import tempfile
from pathlib import Path

import numpy as np

import fungen_compare as fc


def sine_actions(duration_ms, period_ms, amplitude=40, center=50, step_ms=40, t0=0):
    """Erzeugt eine Sinuskurve als Actions-Liste - Fixture "bekannte Phase"."""
    actions = []
    t = 0
    while t <= duration_ms:
        pos = center + amplitude * np.sin(2 * np.pi * t / period_ms)
        actions.append({"at": t0 + t, "pos": round(float(pos), 1)})
        t += step_ms
    return actions


def irregular_actions(t0=0):
    """Wechselnde Frequenz, eine Pause und ein Amplitudenwechsel in einer
    Kurve - Fixtures "unregelmäßige Zyklen", "Pausen", "Amplitudenwechsel"."""
    actions = []
    t = 0
    # Schnelle, kleine Zyklen
    while t < 4000:
        actions.append({"at": t0 + t, "pos": round(50 + 15 * np.sin(2 * np.pi * t / 500), 1)})
        t += 40
    # Pause (Stillstand)
    while t < 6000:
        actions.append({"at": t0 + t, "pos": 50.0})
        t += 40
    # Langsame, große Zyklen (Amplitudenwechsel)
    while t < 12000:
        actions.append({"at": t0 + t, "pos": round(50 + 45 * np.sin(2 * np.pi * t / 2000), 1)})
        t += 40
    return actions


def write_funscript(path, actions, metadata=None):
    path.write_text(json.dumps({"actions": actions, "metadata": metadata or {}}), encoding="utf-8")


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    # --- Bug 2: verschobene Zeitstempel + Phase --------------------------
    reference = sine_actions(20000, 2000, t0=0)
    same_shape_shifted = sine_actions(20000, 2000, t0=837)  # anderer Startzeitpunkt
    result = fc.best_lag_correlation(reference, same_shape_shifted, max_lag_ms=2000, lag_step_ms=50)
    check("findet die richtige Verschiebung (echte Phase, keine Rateannahme)",
          result is not None and abs(result["lag_ms"] - (-837)) <= 50, str(result))
    check("Korrelation nach Lag-Suche ist hoch (echte Übereinstimmung wird gefunden)",
          result is not None and result["r"] > 0.95, str(result))
    check("naive Korrelation bei Lag 0 ist deutlich schlechter (zeigt den alten Fehler)",
          result is not None and result["r_zero_lag"] is not None and
          result["r"] - result["r_zero_lag"] > 0.3,
          f"r={result['r']}, r_zero_lag={result['r_zero_lag']}")

    # --- Bug 4: Orientierung/Vorzeichen -----------------------------------
    inverted = [{"at": a["at"], "pos": 100 - a["pos"]} for a in reference]
    result_inv = fc.best_lag_correlation(reference, inverted, max_lag_ms=200, lag_step_ms=50)
    check("erkennt eine invertierte Übereinstimmung als 'inverted', nicht als schlechten Treffer",
          result_inv is not None and result_inv["orientation"] == "inverted"
          and result_inv["r"] > 0.95, str(result_inv))

    # --- Bug 3: konstantes Signal -> None, nicht 0.0 -----------------------
    constant = [{"at": t, "pos": 100} for t in range(0, 5000, 100)]
    result_const = fc.best_lag_correlation(reference, constant, max_lag_ms=500, lag_step_ms=100)
    check("konstantes Signal ergibt None (undefiniert), nicht 0.0",
          result_const is None, str(result_const))
    check("pearson() selbst liefert None bei Nullvarianz, nicht 0.0",
          fc.pearson([1, 1, 1, 1], [1, 2, 3, 4]) is None, "")

    # --- unregelmäßige Zyklen/Pausen/Amplitude: Form bleibt vergleichbar ---
    irregular_ref = irregular_actions(t0=0)
    irregular_same = irregular_actions(t0=250)
    result_irr = fc.best_lag_correlation(irregular_ref, irregular_same, max_lag_ms=1000, lag_step_ms=50)
    check("findet Übereinstimmung auch bei Pausen/Amplitudenwechsel/wechselnder Frequenz",
          result_irr is not None and result_irr["r"] > 0.9, str(result_irr))
    check("shape_normalized_error ist klein bei identischer Form",
          result_irr is not None and result_irr["shape_error"] is not None
          and result_irr["shape_error"] < 0.3, str(result_irr))

    # --- Bug 1: Duplikate nach INHALT, nicht nach Pfad --------------------
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / "a").mkdir()
        (tmp / "b").mkdir()
        write_funscript(tmp / "a" / "ref.funscript", reference)
        write_funscript(tmp / "b" / "ref_copy.funscript", reference)  # identischer Inhalt
        write_funscript(tmp / "a" / "different.funscript", irregular_ref)
        unique = fc.dedupe_by_content([
            tmp / "a" / "ref.funscript", tmp / "b" / "ref_copy.funscript",
            tmp / "a" / "different.funscript"])
        check("zwei inhaltlich identische Dateien unter verschiedenen Pfaden zählen einmal",
              len(unique) == 2, str([p.name for p in unique]))

    # --- Bug 1b: mehrdeutiger Dateiname-Treffer wird abgelehnt, nicht geraten ---
    stems = {
        "Szene Langer Titel Teil Eins": {"hub": Path("a")},
        "Szene Langer Titel Teil Zwei": {"hub": Path("b")},
    }
    # Beide Stems teilen sich denselben 80-Zeichen-Präfix ("Szene Langer Titel Teil ")
    # -> ein Referenzname mit genau diesem Präfix darf NICHT geraten zugeordnet werden.
    common_prefix = "Szene Langer Titel Teil " + "x" * 60
    stems_ambig = {common_prefix[:80] + " Eins": {"hub": Path("a")},
                   common_prefix[:80] + " Zwei": {"hub": Path("b")}}
    ref_path = Path(common_prefix[:80] + " Drei.funscript")
    stem, reason = fc.match_batch_stem(ref_path, stems_ambig)
    check("mehrdeutiger 80-Zeichen-Präfix-Treffer wird abgelehnt statt geraten",
          stem is None and reason is not None and "ambiguous" in reason, f"{stem}, {reason}")

    exact_stems = {"genau dieser name": {"hub": Path("a")}}
    stem_ok, reason_ok = fc.match_batch_stem(Path("genau dieser name.funscript"), exact_stems)
    check("exakter Namenstreffer funktioniert weiterhin",
          stem_ok == "genau dieser name" and reason_ok is None, f"{stem_ok}, {reason_ok}")

    # --- compare_dataset(): Ende-zu-Ende mit echten Dateien ----------------
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        write_funscript(tmp / "clip.funscript", reference)                    # FunGen-Referenz
        write_funscript(tmp / "clip__hub.funscript", same_shape_shifted)      # guter, verschobener Treffer
        write_funscript(tmp / "clip__tf.funscript", constant)                 # undefiniert -> ausgeschlossen
        write_funscript(tmp / "clip_copy.funscript", reference)               # inhaltliches Duplikat der Referenz
        (tmp / "clip.fungen").write_bytes(b"FGPROJ\x00\x00binaerkram")        # wird NICHT geparst

        result = fc.compare_dataset(str(tmp), max_lag_ms=2000)
        check("genau eine Referenz nach Dedup (Duplikat zählt nicht doppelt)",
              len({r["reference"] for r in result["rows"]} |
                  {e["reference"] for e in result["excluded"]}) == 1,
              str(result["rows"] + result["excluded"]))
        check("hub-Zeile mit hoher Korrelation trotz Zeitverschiebung",
              any(r["kind"] == "hub" and r["r"] > 0.9 for r in result["rows"]),
              str(result["rows"]))
        check("tf (konstant) landet in 'excluded', nicht in 'rows' oder als 0.0",
              any(e["kind"] == "tf" for e in result["excluded"]) and
              not any(r["kind"] == "tf" for r in result["rows"]),
              str(result["excluded"]))
        check(".fungen-Datei wird als übersprungen gemeldet, nie geparst",
              any("fungen" in str(s[0]) for s in result["skipped"]), str(result["skipped"]))

        report = fc.format_report(result)
        check("Bericht nennt die gefundene Verschiebung",
              "lag" in report, "")
        check("Bericht nennt den Ausschlussgrund statt ihn zu verschweigen",
              "Excluded" in report and "undefined" in report, report)
        check("die lange Referenz-Übereinstimmung gilt NICHT als Low-Confidence",
              not any(r["kind"] == "hub" and r["low_confidence"] for r in result["rows"]),
              str(result["rows"]))

    # --- Low-Confidence-Kennzeichnung bei kurzer Überlappung ------------------
    # Eine weite Lag-Suche über einen kurzen Clip kann zufällig eine hohe
    # Korrelation auf einem winzigen Ausschnitt "finden" - genau das beim
    # echten Datensatz beobachtete Problem (Mittelwert 0.696 bei 3s
    # Suchfenster gegen 0.486 bei 500ms). Ergebnisse mit wenig Overlap
    # müssen das markieren, statt unkommentiert wie jede andere Zeile zu
    # erscheinen.
    short_ref = sine_actions(2000, 500, t0=0)
    short_match = sine_actions(2000, 500, t0=50)
    result_short = fc.best_lag_correlation(short_ref, short_match, max_lag_ms=1000, lag_step_ms=50)
    check("kurze Überlappung wird als 'low_confidence' markiert",
          result_short is not None and result_short["low_confidence"] is True,
          str(result_short))
    check("lange Überlappung (obiger hub-Fall) bleibt bei False",
          fc.best_lag_correlation(reference, same_shape_shifted, max_lag_ms=2000,
                                   lag_step_ms=50)["low_confidence"] is False, "")

    # --- rekursives Einlesen von Unterordnern ----------------------------------
    # Der reale Datensatz lag in Unterordnern (funscript_test_clips/,
    # funscript_motion_tracking/, ...) - ein Scan nur der obersten Ebene
    # hätte dort schlicht nichts gefunden.
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / "clips_a").mkdir()
        (tmp / "clips_b").mkdir()
        write_funscript(tmp / "clips_a" / "nested.funscript", reference)
        write_funscript(tmp / "clips_b" / "nested__hub.funscript", same_shape_shifted)

        result_nested = fc.compare_dataset(str(tmp), max_lag_ms=2000)
        check("findet Referenz und Batch-Ausgabe auch in verschiedenen Unterordnern",
              any(r["kind"] == "hub" and r["r"] > 0.9 for r in result_nested["rows"]),
              str(result_nested["rows"]))

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
