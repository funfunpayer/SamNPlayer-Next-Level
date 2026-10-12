#!/usr/bin/env python3
"""golden_clip_benchmark.py - runs a fixed, versioned set of real clips
through the actual generation pipeline and reports/tracks quality and
(where a reference funscript exists) FunGen agreement over time.

Warum: docs/NEXT.md Priorität 2 ("Reproduce before changing the algorithm")
und die "Perception & Motion System 2.0"-Konzeptdatei (Nutzer, 16. September
2026, Phase 1: "Golden-Clip Benchmark + Telemetrie") fordern beide dasselbe -
eine feste, wiederholbare Vergleichsbasis, damit sich Verbesserungen (oder
Regressionen) tatsächlich MESSEN lassen, statt nur als Einzelmessung zu
einem Clip in einer Chat-Unterhaltung zu stehen (siehe die vielen einzelnen
Messungen in docs/NEXT.md, Priorität 2/8).

Die Clips selbst (persönliches Videomaterial) liegen NICHT im Repository -
nur ein Manifest (JSON), das auf lokale Pfade zeigt:

  {
    "clips": [
      {
        "name": "kurzer Bezeichner",
        "video": "/pfad/zum/clip.mp4",
        "roi": [x, y, w, h],
        "roi2": [x, y, w, h],             optional (Tf/Tj)
        "profile": "standard|weich|tf|tj", optional, Standard "standard"
        "backend": "csrt|flow|grid_lk",    optional, Standard "csrt"
        "reference_funscript": "/pfad/zur/fungen-referenz.funscript"  optional
      },
      ...
    ]
  }

Läuft jeden Clip durch einen echten Subprozessaufruf von
generate_funscript.py (derselbe Weg, den generator.go in der GUI nutzt) -
ein kaputter Clip lässt den Lauf nicht abbrechen, er wird als
fehlgeschlagen markiert und der Rest läuft weiter.

Nutzung:
  python3 golden_clip_benchmark.py --manifest golden_clips.json \
      --history benchmark_history.jsonl
"""

import argparse
import json
import statistics
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

import fungen_compare

GENERATOR_SCRIPT = Path(__file__).parent / "generate_funscript.py"


def load_manifest(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    clips = data.get("clips")
    if not isinstance(clips, list) or not clips:
        raise ValueError(f"Manifest {path!r} enthält keine 'clips'-Liste")
    return clips


def _roi_arg(roi):
    return ",".join(str(int(v)) for v in roi)


def build_clip_args(clip, output_path, report_path):
    """Reine Funktion: Manifest-Eintrag -> CLI-Argumente für
    generate_funscript.py. Getrennt von der eigentlichen Ausführung, damit
    sich die Argument-Zusammenstellung ohne echtes Video/Subprozess testen
    lässt."""
    args = ["--video", clip["video"], "--roi", _roi_arg(clip["roi"]),
            "--output", str(output_path), "--report", str(report_path)]
    if clip.get("roi2"):
        args += ["--roi2", _roi_arg(clip["roi2"])]
    profile = clip.get("profile", "standard")
    if profile in ("weich", "tf", "tj"):
        args += ["--profile", profile]
    backend = clip.get("backend")
    if backend and backend != "csrt":
        args += ["--backend", backend]
    return args


def _last_report_entry(report_path):
    path = Path(report_path)
    if not path.exists():
        return None
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines:
        return None
    return json.loads(lines[-1])


def run_clip(clip, python_exe=None, generator_script=None, max_lag_ms=1000):
    """Führt EINEN Clip durch die echte Pipeline und liefert ein
    Ergebnis-dict. Kein Absturz bei einem einzelnen kaputten Clip - ok=False
    mit error, damit ein fehlerhafter Eintrag nicht den ganzen
    Benchmarklauf abbricht (siehe run_benchmark)."""
    python_exe = python_exe or sys.executable
    generator_script = generator_script or str(GENERATOR_SCRIPT)
    name = clip.get("name") or Path(clip["video"]).stem

    with tempfile.TemporaryDirectory() as tmp:
        output_path = Path(tmp) / "out.funscript"
        report_path = Path(tmp) / "report.jsonl"
        args = build_clip_args(clip, output_path, report_path)
        proc = subprocess.run(
            [python_exe, generator_script, *args],
            capture_output=True, text=True, cwd=str(Path(generator_script).parent))
        if proc.returncode != 0:
            return {"name": name, "ok": False,
                    "error": proc.stderr.strip()[-500:] or "unbekannter Fehler"}

        entry = _last_report_entry(report_path)
        quality = (entry or {}).get("quality", {})
        result = {
            "name": name, "ok": True,
            "quality_score": quality.get("score"),
            "quality_passed": quality.get("passed"),
            "quality_warnings": quality.get("warnings", []),
            "correlation": None,
        }

        ref_path = clip.get("reference_funscript")
        if ref_path:
            ref_actions, ref_err = fungen_compare.load_actions(Path(ref_path))
            gen_actions, gen_err = fungen_compare.load_actions(output_path)
            if ref_actions is not None and gen_actions is not None:
                result["correlation"] = fungen_compare.best_lag_correlation(
                    ref_actions, gen_actions, max_lag_ms=max_lag_ms)
            else:
                result["correlation_error"] = ref_err or gen_err
        return result


def summarize(clip_results):
    """Reine Funktion: Liste von run_clip()-Ergebnissen -> Zusammenfassung.
    Mittelwerte ignorieren fehlgeschlagene Läufe und undefinierte
    Korrelationen (None statt 0) - dieselbe Regel wie fungen_compare.pearson()."""
    ok_results = [r for r in clip_results if r["ok"]]
    scores = [r["quality_score"] for r in ok_results if r.get("quality_score") is not None]
    passed = [r for r in ok_results if r.get("quality_passed")]
    correlations = [r["correlation"]["r"] for r in ok_results
                     if r.get("correlation") is not None]
    return {
        "total": len(clip_results),
        "ok": len(ok_results),
        "failed": len(clip_results) - len(ok_results),
        "quality_passed": len(passed),
        "mean_quality_score": statistics.fmean(scores) if scores else None,
        "mean_correlation": statistics.fmean(correlations) if correlations else None,
        "clips_with_reference": len(correlations),
    }


def _git_commit():
    try:
        out = subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                              capture_output=True, text=True, cwd=str(Path(__file__).parent))
        return out.stdout.strip() if out.returncode == 0 else None
    except FileNotFoundError:
        return None


def run_benchmark(manifest_path, python_exe=None, generator_script=None, max_lag_ms=1000,
                   report_progress=True):
    clips = load_manifest(manifest_path)
    results = []
    for i, clip in enumerate(clips):
        if report_progress:
            print(f"PROGRESS {i} {len(clips)}", file=sys.stderr, flush=True)
        results.append(run_clip(clip, python_exe=python_exe,
                                 generator_script=generator_script, max_lag_ms=max_lag_ms))
    if report_progress:
        print(f"PROGRESS {len(clips)} {len(clips)}", file=sys.stderr, flush=True)
    return {
        "timestamp": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "git_commit": _git_commit(),
        "manifest": str(manifest_path),
        "clips": results,
        "summary": summarize(results),
    }


def append_history(history_path, result):
    history_path = Path(history_path)
    history_path.parent.mkdir(parents=True, exist_ok=True)
    with open(history_path, "a", encoding="utf-8") as f:
        f.write(json.dumps(result) + "\n")


def load_history(history_path, limit=20):
    """Letzte `limit` Einträge, neuester zuerst - für die GUI-Verlaufsansicht
    (dasselbe Muster wie app_training.go's Sitzungsverlauf)."""
    path = Path(history_path)
    if not path.exists():
        return []
    entries = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            entries.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return list(reversed(entries[-limit:]))


def format_report(result):
    lines = ["# Golden-Clip Benchmark", ""]
    s = result["summary"]
    lines.append(f"{s['ok']}/{s['total']} Clips erfolgreich, "
                  f"{s['quality_passed']}/{s['ok'] or 1} Quality-Doctor bestanden")
    if s["mean_quality_score"] is not None:
        lines.append(f"Mittlerer Quality-Score: {s['mean_quality_score']:.2f}")
    if s["mean_correlation"] is not None:
        lines.append(f"Mittlere FunGen-Korrelation: {s['mean_correlation']:.3f} "
                      f"({s['clips_with_reference']} Clips mit Referenz)")
    lines.append("")
    for r in result["clips"]:
        if not r["ok"]:
            lines.append(f"- {r['name']}: FEHLGESCHLAGEN - {r['error']}")
            continue
        score = f"{r['quality_score']:.2f}" if r.get("quality_score") is not None else "n/a"
        passed = "OK" if r.get("quality_passed") else "PRÜFEN"
        corr_note = ""
        if r.get("correlation"):
            corr_note = f", r={r['correlation']['r']:.3f}"
        lines.append(f"- {r['name']}: Score {score} ({passed}){corr_note}")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--history", default=None,
                     help="JSONL-Datei, an die ein neuer Eintrag angehängt wird (optional)")
    ap.add_argument("--json-output", default=None,
                     help="Ergebnis zusätzlich als JSON-Datei schreiben (für die GUI, die "
                          "strukturierte Daten statt des Fließtext-Berichts braucht)")
    ap.add_argument("--max-lag-ms", type=int, default=1000)
    args = ap.parse_args()

    result = run_benchmark(args.manifest, max_lag_ms=args.max_lag_ms)
    print(format_report(result))
    if args.history:
        append_history(args.history, result)
        print(f"\nAn Verlauf angehängt: {args.history}", file=sys.stderr)
    if args.json_output:
        Path(args.json_output).write_text(json.dumps(result), encoding="utf-8")


if __name__ == "__main__":
    main()
