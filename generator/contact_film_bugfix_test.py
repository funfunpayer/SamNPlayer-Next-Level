"""Systemische Film-Clip-Bugfix für Kontakt-Vibration + SAM.

Erzeugt synthetische Zwei-Objekt-Clips mit bekanntem Abstandssignal
(wie two_point_test), generiert tj+contact Funscripts, prüft Recipe,
Polarität (eng→hohe Pos), SAM-Enrich und Kurven soft≤peak.

Hinweis: Absolute „nah/fern“-Mittelwerte nach Dynamik-Normierung sind
kein valider Clip-Vergleich — jedes Video spannt auf 20–90. Deshalb
Polarität gegen Truth-Gap und SAM-Intensity.

Ausführen:
  python3 generator/contact_film_bugfix_test.py
"""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path

import cv2
import numpy as np

W, H, FPS = 320, 240, 25


def write_distance_clip(path: Path, seconds: float, gap_fn) -> np.ndarray:
    """Zwei Kreise; gap_fn(t) = Welt-Abstand. Truth = gap über Frames."""
    rng = np.random.default_rng(7)
    bg = np.full((H + 100, W + 100, 3), 45, np.uint8)
    for _ in range(120):
        x, y = int(rng.integers(0, W + 80)), int(rng.integers(0, H + 80))
        cv2.rectangle(bg, (x, y), (x + 12, y + 12),
                      tuple(int(v) for v in rng.integers(70, 160, 3)), -1)
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    truth = []
    n = int(FPS * seconds)
    for i in range(n):
        t = i / FPS
        frame = bg[40:40 + H, 40:40 + W].copy()
        gap = float(gap_fn(t))
        cy = 110
        cv2.circle(frame, (150, int(cy - gap / 2)), 14, (250, 250, 250), -1)
        cv2.circle(frame, (175, int(cy + gap / 2)), 14, (200, 200, 200), -1)
        truth.append(gap)
        vw.write(frame)
    vw.release()
    return np.asarray(truth, dtype=float)


def generate(video: Path, out: Path, *, span=None, curve=None, max_frames=0) -> dict:
    cmd = [
        sys.executable, str(Path(__file__).resolve().parent / "generate_funscript.py"),
        "--video", str(video), "--output", str(out),
        "--roi", "134,54,32,32", "--roi2", "159,104,32,32",
        "--profile", "tj", "--contact-vibration", "--no-cache",
    ]
    if span is not None:
        cmd += ["--contact-vibration-span", str(span)]
    if curve is not None:
        cmd += ["--contact-vibration-curve", curve]
    if max_frames:
        cmd += ["--max-frames", str(max_frames)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"generate failed:\n{r.stderr[-2000:]}")
    return json.loads(out.read_text())


def pos_vs_gap_correlation(doc: dict, truth_gap: np.ndarray, fps: float = FPS) -> float:
    """Tf/Tj: eng (kleiner Gap) → hohe Pos. Korrelation Pos mit −Gap."""
    acts = doc["actions"]
    if len(acts) < 4 or len(truth_gap) < 4:
        return 0.0
    pos, inv_gap = [], []
    for a in acts:
        idx = int(round(a["at"] / 1000.0 * fps))
        idx = max(0, min(len(truth_gap) - 1, idx))
        pos.append(a["pos"])
        inv_gap.append(-float(truth_gap[idx]))
    return float(np.corrcoef(pos, inv_gap)[0, 1])


def sam_peak_intensity(sam_path: Path) -> float:
    doc = json.loads(sam_path.read_text())
    intens = [f["motion"].get("intensity", 0) or 0 for f in doc.get("frames", [])]
    return float(max(intens)) if intens else 0.0


def main() -> int:
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name
              + (f"  [{detail}]" if detail and not cond else ""))
        if not cond:
            failures.append(name)

    root = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="contact-bugfix-") as tmp:
        tmp = Path(tmp)

        # --- Clip A: oszillierender Abstand --------------------------------
        clip_a = tmp / "osc.mp4"
        truth_a = write_distance_clip(
            clip_a, 6.0,
            lambda t: 40 + 50 * (0.5 + 0.5 * np.sin(2 * np.pi * t * 0.8)))
        out_a = tmp / "osc.funscript"
        doc_a = generate(clip_a, out_a, span=0.55, curve="linear", max_frames=150)
        recipe = doc_a["metadata"]["device_recipe"]
        check("A: contact_vibration in recipe", recipe.get("contact_vibration") is True)
        check("A: span 0.55 persisted",
              abs(float(recipe.get("contact_vibration_span", 0)) - 0.55) < 1e-6)
        check("A: profile tj", doc_a["metadata"].get("profile") == "tj")
        check("A: sync suction_position", recipe.get("sync") == "suction_position")
        check("A: Actions vorhanden", len(doc_a["actions"]) >= 4)
        corr_a = pos_vs_gap_correlation(doc_a, truth_a)
        check("A: Pos korreliert mit Nähe (−Gap)", corr_a > 0.35, f"r={corr_a:+.3f}")

        # --- Clip B: fast konstanter großer Abstand ------------------------
        clip_b = tmp / "far.mp4"
        truth_b = write_distance_clip(clip_b, 4.0, lambda t: 95 + 5 * np.sin(2 * np.pi * t))
        out_b = tmp / "far.funscript"
        doc_b = generate(clip_b, out_b, span=0.85, curve="soft", max_frames=100)
        check("B: curve soft persisted",
              doc_b["metadata"]["device_recipe"].get("contact_vibration_curve") == "soft")
        check("B: span 0.85 persisted",
              abs(float(doc_b["metadata"]["device_recipe"].get(
                  "contact_vibration_span", 0)) - 0.85) < 1e-6)
        corr_b = pos_vs_gap_correlation(doc_b, truth_b)
        check("B: keine starke Anti-Korrelation (Polarität)",
              corr_b > -0.35, f"r={corr_b:+.3f}")

        # --- Clip C: stark oszillierend eng/weit ---------------------------
        clip_c = tmp / "close.mp4"
        truth_c = write_distance_clip(
            clip_c, 4.0, lambda t: 25 + 35 * (0.5 + 0.5 * np.sin(2 * np.pi * t)))
        out_c = tmp / "close.funscript"
        doc_c = generate(clip_c, out_c, span=0.5, curve="peak", max_frames=100)
        corr_c = pos_vs_gap_correlation(doc_c, truth_c)
        check("C: Pos korreliert mit Nähe", corr_c > 0.35, f"r={corr_c:+.3f}")
        check("C: peak curve persisted",
              doc_c["metadata"]["device_recipe"].get("contact_vibration_curve") == "peak")

        # --- Span früher/später --------------------------------------------
        out_early = tmp / "early.funscript"
        out_late = tmp / "late.funscript"
        generate(clip_a, out_early, span=0.45, max_frames=120)
        generate(clip_a, out_late, span=0.9, max_frames=120)
        check(
            "Span früher/später unterschiedlich in recipe",
            abs(json.loads(out_early.read_text())["metadata"]["device_recipe"][
                "contact_vibration_span"]
                - json.loads(out_late.read_text())["metadata"]["device_recipe"][
                    "contact_vibration_span"]) > 0.2)

        # --- Gap-Injection -------------------------------------------------
        gap_doc = json.loads(out_c.read_text())
        acts = gap_doc["actions"]
        if len(acts) >= 3:
            mid = acts[len(acts) // 2]["at"]
            gap_doc["metadata"]["tracking_gaps"] = [
                {"start_ms": mid - 50, "end_ms": mid + 150}]
            gap_path = tmp / "gap.funscript"
            gap_path.write_text(json.dumps(gap_doc, indent=2))
            check("Gap-Metadaten schreibbar",
                  "tracking_gaps" in json.loads(gap_path.read_text())["metadata"])

            # Go: PlaybackFrames muss Gap muten
            go_test = subprocess.run(
                ["go", "test", "./sam/", "-run", "TestPlaybackFramesUsesSAMIntensity", "-count=1"],
                cwd=str(root.parent), capture_output=True, text=True)
            check("Go PlaybackFrames Gap-Mute", go_test.returncode == 0,
                  go_test.stdout[-300:] + go_test.stderr[-200:])

        # --- SAM CLI (Flags nach Datei — Regression für splitCLIArgs) ------
        sam_lin = tmp / "osc-enriched.sam"  # anderer Stem als Funscript → beweist --output
        r1 = subprocess.run(
            ["go", "run", "./cmd/cli", "sam", str(out_a), "--output", str(sam_lin)],
            cwd=str(root.parent), capture_output=True, text=True)
        check("SAM CLI convert Exit 0", r1.returncode == 0,
              r1.stderr[-400:] if r1.returncode else "")
        check("SAM CLI --output nach Datei greift",
              sam_lin.exists() and not (tmp / "osc.sam").exists(),
              (r1.stdout or "")[-200:])
        if sam_lin.exists():
            sam_doc = json.loads(sam_lin.read_text())
            check("SAM version 0.1", sam_doc.get("version") == "0.1")
            check("SAM profile tj", sam_doc.get("metadata", {}).get("profile") == "tj")
            check("SAM hat Kontakt-Intensity-Frames",
                  sam_peak_intensity(sam_lin) > 0.2,
                  f"max={sam_peak_intensity(sam_lin):.3f}")
            check("SAM Frames vorhanden", len(sam_doc.get("frames", [])) >= 4)

        # soft vs peak auf denselben Actions (Kurve nur über Recipe → Enrich)
        soft_doc = json.loads(out_c.read_text())
        soft_doc["metadata"]["device_recipe"]["contact_vibration_curve"] = "soft"
        soft_path = tmp / "soft.funscript"
        soft_path.write_text(json.dumps(soft_doc, indent=2))
        sam_soft, sam_peak = tmp / "custom-soft.sam", tmp / "custom-peak.sam"
        r_soft = subprocess.run(
            ["go", "run", "./cmd/cli", "sam", str(soft_path), "--output", str(sam_soft)],
            cwd=str(root.parent), capture_output=True, text=True)
        r_peak = subprocess.run(
            ["go", "run", "./cmd/cli", "sam", str(out_c), "--output", str(sam_peak)],
            cwd=str(root.parent), capture_output=True, text=True)
        check("SAM soft --output Exit 0 + Datei",
              r_soft.returncode == 0 and sam_soft.exists(),
              (r_soft.stderr or r_soft.stdout)[-300:])
        check("SAM peak --output Exit 0 + Datei",
              r_peak.returncode == 0 and sam_peak.exists(),
              (r_peak.stderr or r_peak.stdout)[-300:])

        def intensities(path: Path):
            return [f["motion"].get("intensity") or 0
                    for f in json.loads(path.read_text())["frames"]]

        if sam_soft.exists() and sam_peak.exists():
            soft_i = intensities(sam_soft)
            peak_i = intensities(sam_peak)
            check("soft/peak gleiche Frame-Anzahl", len(soft_i) == len(peak_i) and len(soft_i) >= 2)
            # Nach Dynamik-Normierung sitzen viele Frames an 20/90 → Intensity 0/1.
            # Kurven wirken nur auf Zwischenwerte; peak (√t) ≥ soft (t²) frameweise.
            diffs = [p - s for s, p in zip(soft_i, peak_i)]
            mid_pairs = [(s, p) for s, p in zip(soft_i, peak_i) if 0 < s < 1 or 0 < p < 1]
            check("soft/peak Zwischenwerte vorhanden",
                  len(mid_pairs) >= 1, f"mids={len(mid_pairs)}")
            if mid_pairs:
                check("peak-Kurve ≥ soft-Kurve (frameweise)",
                      all(p + 1e-9 >= s for s, p in mid_pairs)
                      and float(np.mean(peak_i)) >= float(np.mean(soft_i)) - 1e-6,
                      f"mean_peak={np.mean(peak_i):.3f} mean_soft={np.mean(soft_i):.3f} "
                      f"max_diff={max(diffs):.3f}")

        # Default-Sidecar ohne --output
        r_def = subprocess.run(
            ["go", "run", "./cmd/cli", "sam", str(out_a)],
            cwd=str(root.parent), capture_output=True, text=True)
        default_sam = out_a.with_suffix(".sam")
        check("Default-Sidecar neben Funscript",
              r_def.returncode == 0 and default_sam.exists()
              and default_sam.stem == out_a.stem)

    print()
    if failures:
        print("FEHLGESCHLAGEN: " + ", ".join(failures))
        return 1
    print("Alle Film-Clip-Bugfix-Prüfungen bestanden.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
