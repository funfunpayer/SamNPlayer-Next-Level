"""Regressionstest: find_roi_candidates listet Peak-Regionen ohne Auto-Commit.

Ausführen: python3 generator/auto_roi_candidates_test.py
"""

import subprocess
import sys
import tempfile
from pathlib import Path

import auto_roi
from two_point_test import write_video

SCRIPT = Path(__file__).parent / "auto_roi.py"


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory() as tmp:
        video = Path(tmp) / "motion.mp4"
        write_video(video)

        cands = auto_roi.find_roi_candidates(str(video), max_seconds=5, report_progress=False)
        check("mindestens ein Kandidat", len(cands) >= 1, str(len(cands)))
        check("Kandidaten absteigend nach Score",
              all(cands[i]["score"] >= cands[i + 1]["score"] for i in range(len(cands) - 1)),
              str([(c["score"]) for c in cands]))
        for i, c in enumerate(cands):
            check(f"Kandidat {i}: positive Box",
                  c["w"] >= 24 and c["h"] >= 24 and c["x"] >= 0 and c["y"] >= 0,
                  str(c))

        # CLI --list: stdout only CANDIDATE lines, never auto-applies ROI.
        proc = subprocess.run(
            [sys.executable, str(SCRIPT), "--video", str(video),
             "--list", "--max-seconds", "5"],
            capture_output=True, text=True, cwd=str(SCRIPT.parent), timeout=120)
        check("--list Exit-Code 0", proc.returncode == 0, proc.stderr[-400:])
        lines = [ln for ln in proc.stdout.splitlines() if ln.startswith("CANDIDATE ")]
        check("--list gibt CANDIDATE-Zeilen aus", len(lines) >= 1, proc.stdout)
        check("--list setzt kein ROI-alone Commit",
              not any(ln.startswith("ROI ") for ln in proc.stdout.splitlines()),
              proc.stdout)

    print(("FAILED: " + ", ".join(failures)) if failures else "All checks passed.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
