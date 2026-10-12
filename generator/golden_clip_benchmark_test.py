"""Test für golden_clip_benchmark.py - die reinen Funktionen (Argument-
Zusammenstellung, Zusammenfassung, Verlaufs-Lesen/Schreiben, Bericht).

Läuft ohne Video/Subprozess. Der Ende-zu-Ende-Teil (run_clip/run_benchmark
über einen echten Subprozessaufruf) steht in
golden_clip_benchmark_cli_test.py, getrennt aus demselben Grund wie
audio_check.py/audio_check_cli_test.py: nur die reinen Funktionen laufen in
CI's schneller Testsuite.

Ausführen: python3 generator/golden_clip_benchmark_test.py
"""

import json
import sys
import tempfile
from pathlib import Path

import golden_clip_benchmark as bench


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    # --- build_clip_args: Grundfall ------------------------------------------
    clip = {"video": "x.mp4", "roi": [1, 2, 3, 4]}
    args = bench.build_clip_args(clip, "out.funscript", "report.jsonl")
    check("enthält --video/--roi/--output/--report",
          all(a in args for a in ["--video", "x.mp4", "--roi", "1,2,3,4",
                                   "--output", "out.funscript", "--report", "report.jsonl"]),
          str(args))
    check("kein --roi2/--profile/--backend ohne Angabe im Manifest",
          not any(a in args for a in ["--roi2", "--profile", "--backend"]), str(args))

    # --- build_clip_args: roi2/profile/backend --------------------------------
    clip2 = {"video": "x.mp4", "roi": [1, 2, 3, 4], "roi2": [5, 6, 7, 8],
              "profile": "tj", "backend": "grid_lk"}
    args2 = bench.build_clip_args(clip2, "out.funscript", "report.jsonl")
    check("übernimmt roi2", "--roi2" in args2 and "5,6,7,8" in args2, str(args2))
    check("übernimmt profile", "--profile" in args2 and "tj" in args2, str(args2))
    check("übernimmt backend", "--backend" in args2 and "grid_lk" in args2, str(args2))

    # --- build_clip_args: Standard-Backend "csrt" wird NICHT als Flag gesetzt
    clip3 = {"video": "x.mp4", "roi": [1, 2, 3, 4], "backend": "csrt"}
    args3 = bench.build_clip_args(clip3, "out.funscript", "report.jsonl")
    check("Backend 'csrt' erzeugt kein --backend-Flag (ist ohnehin Standard)",
          "--backend" not in args3, str(args3))

    # --- summarize: gemischte Ergebnisse --------------------------------------
    results = [
        {"name": "a", "ok": True, "quality_score": 0.9, "quality_passed": True,
         "correlation": {"r": 0.5}},
        {"name": "b", "ok": True, "quality_score": 0.4, "quality_passed": False,
         "correlation": None},
        {"name": "c", "ok": False, "error": "kaputt"},
    ]
    s = bench.summarize(results)
    check("total zählt alle Ergebnisse", s["total"] == 3, str(s))
    check("ok zählt nur erfolgreiche", s["ok"] == 2, str(s))
    check("failed zählt den Rest", s["failed"] == 1, str(s))
    check("quality_passed zählt nur bestandene", s["quality_passed"] == 1, str(s))
    check("mean_quality_score ignoriert den fehlgeschlagenen Clip",
          abs(s["mean_quality_score"] - 0.65) < 1e-9, str(s))
    check("mean_correlation ignoriert undefinierte (None) Korrelationen",
          s["mean_correlation"] == 0.5, str(s))
    check("clips_with_reference zählt nur definierte Korrelationen",
          s["clips_with_reference"] == 1, str(s))

    # --- summarize: komplett leer -> keine Division durch Null ---------------
    empty = bench.summarize([])
    check("leere Ergebnisliste liefert None-Mittelwerte statt Absturz",
          empty["mean_quality_score"] is None and empty["mean_correlation"] is None, str(empty))

    # --- format_report: läuft ohne Absturz und nennt die Kernzahlen ----------
    report_text = bench.format_report({"summary": s, "clips": results})
    check("Bericht nennt die Clip-Namen", all(name in report_text for name in ("a", "b", "c")),
          report_text)
    check("Bericht nennt den fehlgeschlagenen Clip als FEHLGESCHLAGEN",
          "c: FEHLGESCHLAGEN" in report_text, report_text)

    # --- append_history/load_history: Rundlauf, neuester zuerst --------------
    with tempfile.TemporaryDirectory() as tmp:
        history_path = Path(tmp) / "sub" / "history.jsonl"
        bench.append_history(history_path, {"timestamp": "1", "summary": s, "clips": []})
        bench.append_history(history_path, {"timestamp": "2", "summary": s, "clips": []})
        loaded = bench.load_history(history_path)
        check("load_history liefert beide Einträge", len(loaded) == 2, str(loaded))
        check("load_history liefert neuesten Eintrag zuerst",
              loaded[0]["timestamp"] == "2", str(loaded))

        check("load_history auf nicht vorhandener Datei liefert leere Liste statt Absturz",
              bench.load_history(Path(tmp) / "fehlt.jsonl") == [], "")

    # --- load_manifest: Rundlauf und Fehlerfall -------------------------------
    with tempfile.TemporaryDirectory() as tmp:
        manifest_path = Path(tmp) / "manifest.json"
        manifest_path.write_text(json.dumps({"clips": [clip]}), encoding="utf-8")
        loaded_clips = bench.load_manifest(manifest_path)
        check("load_manifest liefert die clips-Liste", loaded_clips == [clip], str(loaded_clips))

        empty_manifest = Path(tmp) / "empty.json"
        empty_manifest.write_text(json.dumps({"clips": []}), encoding="utf-8")
        raised = False
        try:
            bench.load_manifest(empty_manifest)
        except ValueError:
            raised = True
        check("leeres Manifest wirft einen klaren Fehler statt eines leeren Laufs", raised, "")

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
