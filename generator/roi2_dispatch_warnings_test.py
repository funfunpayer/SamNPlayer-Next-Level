"""Regressionstest: --roi2 (Zwei-Punkt-Messung) hat einen eigenen
Dispatch-Zweig in generate_funscript.py, der weder --per-scene-roi noch
alle --backend-Werte kennt (nur csrt/grid_lk haben einen Zwei-Punkt-Pfad).
Beide Fälle wurden bisher kommentarlos ignoriert - das Häkchen/die Option
war gesetzt und tat nichts, ohne jede Rückmeldung.

Zwei-Punkt-Hints gelten nur bei --profile tf/tj. Stroke/Autotune keep
--roi2 as contact_marks metadata (feel) and tip-track only (#145 / #211).

Geprüft wird nur die Sichtbarkeit des jetzt ausgegebenen Hinweises auf
stderr (nicht die Tracking-Qualität selbst - die ist bereits durch
two_point_test.py/grid_lk_two_point_test.py abgedeckt), sowie dass der
Lauf trotzdem eine Ausgabedatei erzeugt statt abzubrechen.

Ausführen: python3 generator/roi2_dispatch_warnings_test.py
"""

import json
import subprocess
import sys
import tempfile
from pathlib import Path

from two_point_test import ROI_A, ROI_B, write_video

SCRIPT = Path(__file__).parent / "generate_funscript.py"


def roi_str(roi):
    return ",".join(str(v) for v in roi)


def run(video, output, *extra_args):
    return subprocess.run(
        [sys.executable, str(SCRIPT), "--video", str(video),
         "--roi", roi_str(ROI_A), "--roi2", roi_str(ROI_B),
         "--output", str(output), "--max-frames", "15", "--no-cache",
         *extra_args],
        capture_output=True, text=True, cwd=str(SCRIPT.parent), timeout=60)


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory() as tmp:
        video = Path(tmp) / "two_point.mp4"
        write_video(video)

        # Stroke/Autotune + roi2: tip curve; contact mark stored (#211).
        out0 = Path(tmp) / "autotune.funscript"
        result = run(video, out0, "--profile", "autotune")
        check("autotune + roi2: Exit-Code 0", result.returncode == 0, result.stderr)
        check("autotune + roi2: tip-only / contact-mark hint on stderr",
              ("storing --roi2 as contact mark" in result.stderr
               or "ignoring --roi2" in result.stderr)
              and ("tip CSRT writes the stroke" in result.stderr
                   or "tip ROI only" in result.stderr),
              result.stderr)
        check("autotune + roi2: output file still created", out0.exists())
        if out0.exists():
            meta = json.loads(out0.read_text()).get("metadata") or {}
            cm = meta.get("contact_marks") or {}
            primary = cm.get("primary") or {}
            check("autotune + roi2: contact_marks stamped",
                  primary.get("w") == ROI_B[2] and primary.get("h") == ROI_B[3],
                  str(cm))
            check("autotune + roi2: drive_stroke false",
                  cm.get("drive_stroke") is False, str(cm))

        # --per-scene-roi zusammen mit --roi2 (Tf/Tj): bisher stillschweigend
        # wirkungslos (der roi2-Zweig kommt vor dem per-scene-roi-Zweig
        # und kennt keine Szenen-Neuerkennung).
        out1 = Path(tmp) / "perscene.funscript"
        result = run(video, out1, "--profile", "tf", "--per-scene-roi")
        check("per-scene-roi + roi2: Exit-Code 0", result.returncode == 0, result.stderr)
        check("per-scene-roi + roi2: hint on stderr",
              "per-scene-roi" in result.stderr and "has no effect" in result.stderr,
              result.stderr)
        check("per-scene-roi + roi2: output file still created", out1.exists())

        # --backend flow zusammen mit --roi2 (Tf/Tj): flow hat keinen
        # Zwei-Punkt-Pfad, fällt bisher stillschweigend auf CSRT zurück.
        out2 = Path(tmp) / "flow.funscript"
        result = run(video, out2, "--profile", "tf", "--backend", "flow")
        check("backend flow + roi2: Exit-Code 0", result.returncode == 0, result.stderr)
        check("backend flow + roi2: hint on stderr",
              "'flow'" in result.stderr and "using CSRT" in result.stderr,
              result.stderr)
        check("backend flow + roi2: output file still created", out2.exists())

        # Normalfall Tf/Tj (csrt, kein per-scene-roi): kein Hinweis, keine
        # falsch-positive Warnung.
        out3 = Path(tmp) / "normal.funscript"
        result = run(video, out3, "--profile", "tf")
        check("normal two-point run: Exit-Code 0", result.returncode == 0, result.stderr)
        check("normal two-point run: no hint on stderr",
              "has no effect" not in result.stderr and "using CSRT" not in result.stderr,
              result.stderr)

        # backend grid_lk + roi2 (Tf/Tj): hat einen eigenen Zwei-Punkt-Pfad,
        # darf nicht fälschlich als "nicht unterstützt" gemeldet werden.
        out4 = Path(tmp) / "gridlk.funscript"
        result = run(video, out4, "--profile", "tf", "--backend", "grid_lk")
        check("backend grid_lk + roi2: Exit-Code 0", result.returncode == 0, result.stderr)
        check("backend grid_lk + roi2: no fallback hint",
              "using CSRT" not in result.stderr, result.stderr)

    print(("FAILED: " + ", ".join(failures)) if failures else "All checks passed.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
