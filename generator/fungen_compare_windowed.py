#!/usr/bin/env python3
"""fungen_compare_windowed.py - runs fungen_compare.py's best_lag_correlation
independently per fixed-size absolute-time window, instead of once for an
entire clip.

Promoted from the ad hoc script used for the first real Tf/Tj golden-clip
measurement (21 Sep 2026, see docs/NEXT.md "First real Tf/Tj golden-clip
measurement"): whole-clip correlation on
generator/testdata/golden_clips/clip_voll_tftj read as near-zero (r=0.06),
which looked like broken tracking or wrong ROI marking. Splitting the same
clip into 30s windows and running best_lag_correlation per window instead
showed r=0.32 with the lag drifting -2600..+2800ms across the clip - a
single whole-clip lag search cannot represent a DRIFTING offset, it just
averages the well-aligned and badly-aligned parts into noise. See
docs/FINDINGS_TIMING_TF.md F-003 (FrameIndex/FPS imprecision under VFR).

Stays Python, same as fungen_compare.py itself, which this module imports
from directly - this is offline analysis tooling, not the runtime
generate path, so the project's "Go only where it measurably helps"
policy (docs/NEXT.md, trackcv entry) does not apply here.

Usage:
  python3 generator/fungen_compare_windowed.py REFERENCE.funscript VARIANT.funscript \\
      --window-ms 30000 --max-lag-ms 3000
"""

import argparse
import sys
from pathlib import Path

import fungen_compare as fc


def windowed_correlation(actions_a, actions_b, window_ms=30000, max_lag_ms=1000,
                          lag_step_ms=100, resample_step_ms=100):
    """Splits series `a`'s absolute timeline into window_ms windows and runs
    fc.best_lag_correlation independently on each window's actions from `a`
    against `b`'s actions padded by max_lag_ms on both sides (so a lag
    search near a window edge is not starved of `b` actions to shift in).

    Returns a list of per-window dicts: window_start_ms, window_end_ms, plus
    whatever best_lag_correlation returns (r, lag_ms, orientation, ...), or
    r=None + a reason when a window has too few actions or an undefined
    (near-constant) correlation - never a silently-dropped window, so a
    quiet clip section shows up as "undefined", not as a missing row.
    """
    a_sorted = sorted(actions_a, key=lambda x: int(x["at"]))
    b_sorted = sorted(actions_b, key=lambda x: int(x["at"]))
    t0, t1 = int(a_sorted[0]["at"]), int(a_sorted[-1]["at"])

    results = []
    start = t0
    while start < t1:
        end = min(start + window_ms, t1)
        a_slice = [x for x in a_sorted if start <= int(x["at"]) <= end]
        pad = max_lag_ms
        b_slice = [x for x in b_sorted if start - pad <= int(x["at"]) <= end + pad]
        row = {"window_start_ms": start, "window_end_ms": end}
        if len(a_slice) < 2 or len(b_slice) < 2:
            row["r"] = None
            row["reason"] = "too_few_actions_in_window"
        else:
            best = fc.best_lag_correlation(a_slice, b_slice, max_lag_ms=max_lag_ms,
                                            lag_step_ms=lag_step_ms,
                                            resample_step_ms=resample_step_ms)
            if best is None:
                row["r"] = None
                row["reason"] = "undefined_correlation"
            else:
                row.update(best)
        results.append(row)
        start = end
    return results


def format_report(results, ref_name, variant_name, window_ms):
    lines = [f"# Windowed FunGen comparison ({window_ms}ms windows)", "",
             f"Reference: {ref_name}  ·  Variant: {variant_name}", ""]
    confident = [r for r in results if r.get("r") is not None and not r.get("low_confidence")]
    for r in results:
        w = f"{r['window_start_ms']/1000:.1f}-{r['window_end_ms']/1000:.1f}s"
        if r.get("r") is None:
            lines.append(f"- {w}: undefined ({r.get('reason', 'n/a')})")
            continue
        orient_note = " INVERTED" if r["orientation"] == "inverted" else ""
        low_note = " [LOW CONFIDENCE]" if r.get("low_confidence") else ""
        lines.append(f"- {w}: r={r['r']:.3f} @ lag {r['lag_ms']:+d}ms{orient_note}{low_note}")
    if confident:
        mean_r = sum(r["r"] for r in confident) / len(confident)
        lags = [r["lag_ms"] for r in confident]
        orientations = {r["orientation"] for r in confident}
        lines.append("")
        lines.append(f"## Summary (n={len(confident)} confident windows of {len(results)} total)")
        lines.append(f"- mean r: {mean_r:.3f}")
        lines.append(f"- lag range: {min(lags):+d}ms .. {max(lags):+d}ms")
        if len(orientations) > 1:
            lines.append("- orientation FLIPS across the clip (see per-window rows above) - "
                          "check the source video around the flip, this is the one finding "
                          "that could be a marking issue rather than timing drift")
    else:
        lines.append("")
        lines.append("## Summary: no confident windows")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("reference", metavar="REFERENCE.funscript")
    ap.add_argument("variant", metavar="VARIANT.funscript")
    ap.add_argument("--window-ms", type=int, default=30000,
                     help="Window size in ms (default 30000 = 30s).")
    ap.add_argument("--max-lag-ms", type=int, default=1000,
                     help="Lag search window per side, per window (default 1000ms).")
    ap.add_argument("--output", default=None, metavar="DATEI",
                     help="Write the report here. Default: print to stdout.")
    args = ap.parse_args()

    ref_path, variant_path = Path(args.reference), Path(args.variant)
    ref_actions, ref_reason = fc.load_actions(ref_path)
    variant_actions, variant_reason = fc.load_actions(variant_path)
    if ref_actions is None:
        print(f"Konnte Referenz nicht lesen: {ref_reason}", file=sys.stderr)
        return 1
    if variant_actions is None:
        print(f"Konnte Variante nicht lesen: {variant_reason}", file=sys.stderr)
        return 1

    results = windowed_correlation(ref_actions, variant_actions, window_ms=args.window_ms,
                                    max_lag_ms=args.max_lag_ms)
    report = format_report(results, ref_path.name, variant_path.name, args.window_ms)
    if args.output:
        Path(args.output).write_text(report, encoding="utf-8")
        print(f"Geschrieben: {args.output}", file=sys.stderr)
    else:
        print(report)
    return 0


if __name__ == "__main__":
    sys.exit(main())
