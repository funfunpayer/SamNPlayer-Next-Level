#!/usr/bin/env python3
"""fungen_compare.py - compares SamNPlayer Hub/Tf/Tj `.funscript` output
against FunGen `.funscript` reference exports.

Written for docs/FUNGEN_PARITY_PLAN.md, Work Package 1 ("Establish a
trustworthy benchmark"). This replaces a local, not-in-repository script
(`_compare_fungen.py`) whose output (`compare-fungen-manual.md`, see
issue #8) is the current baseline. Reviewing that script surfaced four
measurement bugs, each of which independently pushes correlation toward
zero regardless of how good the underlying tracking actually is - meaning
the "near-zero correlation" finding may overstate the real gap. Fixed here:

1. DUPLICATE FILES COUNTED TWICE. The local script de-duplicated candidate
   reference files only by resolved file PATH. The same reference present
   under two scanned folders (plausible: exported once, then copied into
   the batch's test folder) produced two identical rows and inflated the
   aggregate's sample count - `compare-fungen-manual.md` shows exactly
   this for two of its four distinct clips. Fixed here by de-duplicating
   on file CONTENT hash (`_content_hash`), not path.

2. CORRELATION SILENTLY ASSUMED BOTH FILES START AT THE SAME WALL-CLOCK
   TIME, WITH NO LAG SEARCH. The local script resampled each file onto ITS
   OWN [first action, last action] window, then zipped the two resulting
   position lists by INDEX. Any difference between the files' first-action
   timestamps - plausible whenever the reference and SamNPlayer disagree
   by even one frame about where the scene "starts" - misaligns every
   sample after it, and a real but time-shifted match then scores as a
   mismatch. Fixed here (`best_lag_correlation`) by resampling both onto
   one SHARED absolute timeline and searching a small lag window for the
   best alignment, matching docs/FUNGEN_PARITY_PLAN.md: "Fair comparison
   needs phase lag."

3. A CONSTANT REFERENCE SCORED 0.0, NOT "UNDEFINED". Pearson correlation
   is undefined when either series has zero variance; the local script's
   division guard returned 0.0 in that case, which then entered the
   aggregate mean as if it were a real (weak) correlation instead of being
   excluded, exactly the case docs/FUNGEN_PARITY_PLAN.md calls out ("One
   reference contains only two equal-position actions... excluded from
   the aggregate"). Fixed here (`pearson`) by returning None and reporting
   the exclusion explicitly instead of silently folding it into the mean.

4. NO CHECK FOR SIGN/ORIENTATION MISMATCH. A perfectly-shaped but
   polarity-inverted signal (a "further from camera" convention mapped to
   a higher position number in one tool and a lower one in the other)
   shows as a strongly NEGATIVE correlation and looks like bad tracking
   rather than a convention mismatch - several `hub` rows in
   `compare-fungen-manual.md` are negative. Fixed here by also testing the
   inverted series (100 - pos) at every candidate lag and reporting
   whichever orientation fits better, never only ever testing one. Per
   docs/FUNGEN_PARITY_PLAN.md ("Do not optimize absolute correlation:
   opposite phase is not equivalent"), an inverted match is reported AS
   inverted, not silently folded into a same-orientation number.

Intentionally NOT read or parsed here: `.fungen` project files (binary,
`FGPROJ` header). Only `.funscript` JSON exports - FunGen's interchange
format, not its project format - are read, the same restriction the local
script already applied; see HANDOFF.md "Third-party code policy".

NOT yet implemented (see docs/FUNGEN_PARITY_PLAN.md "Reproduce before
changing the algorithm" for the full ask): a persisted manifest with
hashes/generator-versions/ROIs/parameters, event-timing precision/recall,
and per-clip plots. This script covers the comparison-correctness half of
Work Package 1; the provenance-manifest half is a follow-up.

Usage:
  python3 fungen_compare.py --dataset DIR --output report.md
"""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

import numpy as np

BATCH_SUFFIX_RE = re.compile(r"__(hub|tf|tj)\.funscript$")
MIN_OVERLAP_SAMPLES = 10
# Unterhalb dieser Sample-Zahl (bei 100ms Schrittweite: 3s Überlappung)
# markiert das Ergebnis als "low confidence" statt es unkommentiert wie
# jede andere Zeile zu präsentieren - ein weiter Lag-Bereich kann sonst auf
# sehr kurzen, zufällig gut passenden Ausschnitten eine hohe Korrelation
# "finden", die auf einem längeren Überlappungsfenster verschwindet (beim
# echten Datensatz beobachtet: r stieg von 0.696 auf 0.486 Mittelwert, als
# das Lag-Fenster von 3s auf 500ms verkleinert wurde).
LOW_CONFIDENCE_SAMPLES = 30


def _content_hash(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dedupe_by_content(paths):
    """Reduces a file list to one entry per distinct CONTENT, keeping the
    first path seen. Fixes bug 1: the same file under two paths (two scan
    roots, a copy) must count once, not once per path."""
    seen = set()
    unique = []
    for path in paths:
        digest = _content_hash(path)
        if digest in seen:
            continue
        seen.add(digest)
        unique.append(path)
    return unique


def load_actions(path):
    """Parses a `.funscript` JSON file. Returns (actions, metadata) or
    (None, reason). Never attempts to read `.fungen` binaries - callers
    should skip those before calling this, exactly like the local script
    did (see module docstring)."""
    try:
        data = json.loads(path.read_text(encoding="utf-8", errors="replace"))
    except (json.JSONDecodeError, OSError) as exc:
        return None, f"json_fail:{exc}"
    if not isinstance(data, dict):
        return None, "not_an_object"
    actions = data.get("actions")
    if actions is None and isinstance(data.get("funscript"), dict):
        actions = data["funscript"].get("actions")
    if not isinstance(actions, list) or len(actions) < 2:
        return None, "no_actions"
    return actions, (data.get("metadata") or {})


def _resample_absolute(actions, t_start, t_end, step_ms):
    """Samples `actions` at fixed absolute times [t_start, t_end], step_ms
    apart - NOT the series' own local window (that was bug 2). Clamps to
    the nearest real action outside the series' own timestamp range rather
    than extrapolating."""
    acts = sorted(actions, key=lambda a: int(a["at"]))
    times = np.array([int(a["at"]) for a in acts], dtype=float)
    positions = np.array([float(a["pos"]) for a in acts], dtype=float)
    sample_times = np.arange(t_start, t_end + 1, step_ms, dtype=float)
    return np.interp(sample_times, times, positions)


def pearson(a, b):
    """Pearson correlation, or None if either series has ~zero variance
    (fixes bug 3: the local script returned 0.0 for this, folding an
    undefined comparison into the aggregate as if it were a weak one)."""
    a, b = np.asarray(a, dtype=float), np.asarray(b, dtype=float)
    if len(a) < 2 or np.std(a) < 1e-9 or np.std(b) < 1e-9:
        return None
    return float(np.corrcoef(a, b)[0, 1])


def shape_normalized_error(a, b):
    """Mean absolute error after z-score normalizing both series - unlike
    raw MAE, this is not dominated by an amplitude or device-range
    difference (e.g. Tf/Tj's 20-90 clamp vs Hub's 0-100), so it isolates
    SHAPE agreement the way docs/FUNGEN_PARITY_PLAN.md asks for
    ("a documented shape-normalized error") separately from raw MAE."""
    a, b = np.asarray(a, dtype=float), np.asarray(b, dtype=float)
    std_a, std_b = np.std(a), np.std(b)
    if std_a < 1e-9 or std_b < 1e-9:
        return None
    za = (a - np.mean(a)) / std_a
    zb = (b - np.mean(b)) / std_b
    return float(np.mean(np.abs(za - zb)))


def best_lag_correlation(actions_a, actions_b, max_lag_ms=1000, lag_step_ms=100,
                          resample_step_ms=100):
    """Searches lag and orientation for the best-matching alignment of two
    funscripts (fixes bugs 2 and 4). Returns a dict:

        r               signed Pearson correlation at the best lag/orientation
        lag_ms          how far `b` was shifted to align with `a` (b's time + lag_ms)
        orientation     "normal" or "inverted" (b compared as 100 - pos)
        r_zero_lag      the naive same-index, same-orientation correlation,
                        for comparison against the old method
        n_samples       samples used at the best lag
        shape_error     shape_normalized_error() at the best lag/orientation
        low_confidence  True if n_samples < LOW_CONFIDENCE_SAMPLES - a wide
                        lag search over a short clip can "find" a high
                        correlation on a small, coincidentally-matching
                        overlap window that a longer window would not
                        support; see LOW_CONFIDENCE_SAMPLES. The default
                        max_lag_ms is deliberately conservative (1000ms, not
                        docs/FUNGEN_PARITY_PLAN.md's exploratory ±2-3s) for
                        the same reason - widen it deliberately, not as the
                        default, and read low_confidence rows skeptically
                        either way.

    None fields mean "undefined" (e.g. a constant series), never 0.0 -
    see pearson().
    """
    a_sorted = sorted(actions_a, key=lambda a: int(a["at"]))
    b_sorted = sorted(actions_b, key=lambda a: int(a["at"]))
    a_t0, a_t1 = int(a_sorted[0]["at"]), int(a_sorted[-1]["at"])
    b_t0, b_t1 = int(b_sorted[0]["at"]), int(b_sorted[-1]["at"])

    best = None
    zero_lag_r = None
    for lag_ms in range(-max_lag_ms, max_lag_ms + 1, lag_step_ms):
        # b's samples, shifted by lag_ms, overlapped against a's own range.
        overlap_start = max(a_t0, b_t0 + lag_ms)
        overlap_end = min(a_t1, b_t1 + lag_ms)
        n_samples = int((overlap_end - overlap_start) // resample_step_ms) + 1
        if overlap_end <= overlap_start or n_samples < MIN_OVERLAP_SAMPLES:
            continue

        series_a = _resample_absolute(a_sorted, overlap_start, overlap_end, resample_step_ms)
        # Sample b at (t - lag_ms) in b's own timeline, i.e. shift b forward by lag_ms.
        shifted_b = [{"at": int(act["at"]) + lag_ms, "pos": act["pos"]} for act in b_sorted]
        series_b = _resample_absolute(shifted_b, overlap_start, overlap_end, resample_step_ms)

        r_normal = pearson(series_a, series_b)
        r_inverted = pearson(series_a, 100.0 - series_b) if r_normal is not None else None

        for r, orientation, sb in (
            (r_normal, "normal", series_b),
            (r_inverted, "inverted", 100.0 - series_b),
        ):
            if r is None:
                continue
            if best is None or r > best["r"]:
                best = {
                    "r": r, "lag_ms": lag_ms, "orientation": orientation,
                    "n_samples": len(series_a),
                    "shape_error": shape_normalized_error(series_a, sb),
                }
        if lag_ms == 0 and r_normal is not None:
            zero_lag_r = r_normal

    if best is not None:
        best["r_zero_lag"] = zero_lag_r
        best["low_confidence"] = best["n_samples"] < LOW_CONFIDENCE_SAMPLES
    return best


def match_batch_stem(reference_path, batch_stems):
    """Matches a reference filename to a SamNPlayer batch stem. Exact stem
    match first; otherwise an 80-character-prefix match, but ONLY if it is
    UNIQUE (fixes the ambiguous-match half of bug 1: docs/FUNGEN_PARITY_
    PLAN.md: "rejecting ambiguous matches" - the local script's fuzzy
    `startswith` search could silently pick the wrong clip when two
    filenames share a long common prefix).

    Returns (stem, None) on a clean match, or (None, reason) otherwise.
    """
    base = reference_path.stem
    if base in batch_stems:
        return base, None
    short = base[:80].rstrip()
    candidates = [s for s in batch_stems if s[:80].rstrip() == short]
    if len(candidates) == 1:
        return candidates[0], None
    if len(candidates) > 1:
        return None, f"ambiguous_match:{candidates}"
    return None, "no_match"


def collect_batch_stems(dataset_dir):
    """stem -> {"hub"|"tf"|"tj": Path}, deduplicated by content first so a
    copied batch output doesn't create a phantom second stem entry."""
    all_variants = list(dataset_dir.rglob("*.funscript"))
    variant_paths = [p for p in all_variants if BATCH_SUFFIX_RE.search(p.name)]
    variant_paths = dedupe_by_content(variant_paths)
    stems = {}
    for path in variant_paths:
        match = BATCH_SUFFIX_RE.search(path.name)
        stem = path.name[:match.start()]
        stems.setdefault(stem, {})[match.group(1)] = path
    return stems


def collect_references(dataset_dir):
    """`.funscript` files that are NOT a hub/tf/tj batch variant, i.e. the
    FunGen reference exports. `.fungen` binaries are listed as explicit
    skips, never parsed (see module docstring)."""
    references, skipped = [], []
    for path in sorted(dataset_dir.rglob("*.fungen")):
        skipped.append((path, "fungen_binary_not_read"))
    ref_candidates = [p for p in dataset_dir.rglob("*.funscript")
                       if not BATCH_SUFFIX_RE.search(p.name)]
    references = dedupe_by_content(ref_candidates)
    return references, skipped


def compare_dataset(dataset_dir, max_lag_ms=1000):
    """Runs the full comparison over one dataset directory. Returns a dict
    with `rows` (one per reference x kind), `excluded` (constant/undefined
    references, reported not hidden), and `skipped` (unreadable files)."""
    dataset_dir = Path(dataset_dir)
    batch_stems = collect_batch_stems(dataset_dir)
    references, skipped = collect_references(dataset_dir)

    rows, excluded = [], []
    for ref_path in sorted(references, key=lambda p: p.name.lower()):
        ref_actions, ref_meta = load_actions(ref_path)
        if ref_actions is None:
            skipped.append((ref_path, ref_meta))
            continue
        stem, reason = match_batch_stem(ref_path, batch_stems)
        if stem is None:
            skipped.append((ref_path, reason))
            continue
        for kind in ("hub", "tf", "tj"):
            variant_path = batch_stems[stem].get(kind)
            if variant_path is None:
                continue
            variant_actions, variant_meta = load_actions(variant_path)
            if variant_actions is None:
                skipped.append((variant_path, variant_meta))
                continue
            result = best_lag_correlation(ref_actions, variant_actions, max_lag_ms=max_lag_ms)
            if result is None:
                excluded.append({
                    "reference": ref_path.name, "kind": kind,
                    "reason": "undefined_correlation (constant series or no overlap)",
                })
                continue
            rows.append({
                "reference": ref_path.name, "stem": stem, "kind": kind, **result,
            })
    return {"rows": rows, "excluded": excluded, "skipped": skipped}


def format_report(result):
    lines = ["# FunGen reference comparison (fungen_compare.py)", ""]
    lines.append(f"Compared rows: {len(result['rows'])} · "
                 f"excluded (undefined): {len(result['excluded'])} · "
                 f"skipped (unreadable/unmatched): {len(result['skipped'])}")
    lines.append("")
    for row in result["rows"]:
        lag_note = f"{row['lag_ms']:+d}ms" if row["lag_ms"] else "0ms"
        orient_note = " INVERTED" if row["orientation"] == "inverted" else ""
        shape = f"{row['shape_error']:.3f}" if row["shape_error"] is not None else "n/a"
        zero = f"{row['r_zero_lag']:.3f}" if row["r_zero_lag"] is not None else "n/a"
        low_conf_note = " [LOW CONFIDENCE: short overlap]" if row.get("low_confidence") else ""
        lines.append(
            f"- {row['reference']} vs **{row['kind']}**: "
            f"r={row['r']:.3f} @ lag {lag_note}{orient_note} "
            f"(r at zero-lag/normal: {zero}) shape_err={shape} n={row['n_samples']}"
            f"{low_conf_note}")
    if result["excluded"]:
        lines.append("")
        lines.append("## Excluded (undefined correlation, not counted in any mean)")
        for item in result["excluded"]:
            lines.append(f"- {item['reference']} vs {item['kind']}: {item['reason']}")
    if result["skipped"]:
        lines.append("")
        lines.append("## Skipped")
        for path, reason in result["skipped"]:
            name = path.name if hasattr(path, "name") else path
            lines.append(f"- {name}: {reason}")
    by_kind = {}
    confident_by_kind = {}
    for row in result["rows"]:
        by_kind.setdefault(row["kind"], []).append(row["r"])
        if not row.get("low_confidence"):
            confident_by_kind.setdefault(row["kind"], []).append(row["r"])
    if by_kind:
        lines.append("")
        lines.append("## Summary")
        for kind in sorted(by_kind):
            values = by_kind[kind]
            lines.append(f"- mean r ({kind}, all, n={len(values)}): {sum(values)/len(values):.3f}")
            confident = confident_by_kind.get(kind, [])
            if confident:
                lines.append(f"  - excluding low-confidence rows (n={len(confident)}): "
                              f"{sum(confident)/len(confident):.3f}")
            else:
                lines.append("  - no rows above the low-confidence sample threshold")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dataset", required=True, metavar="DIR",
                     help="Folder with FunGen .funscript references and "
                          "SamNPlayer *__hub/tf/tj.funscript batch output.")
    ap.add_argument("--output", default=None, metavar="DATEI",
                     help="Write the report here. Default: print to stdout.")
    ap.add_argument("--max-lag-ms", type=int, default=1000,
                     help="Lag search window in each direction (default 1000ms). "
                          "Widening this trades false negatives (a real but larger "
                          "delay scored as a mismatch) for false positives on short "
                          "clips (see LOW_CONFIDENCE_SAMPLES in the module docstring) "
                          "- prefer widening deliberately over raising the default.")
    args = ap.parse_args()

    result = compare_dataset(args.dataset, max_lag_ms=args.max_lag_ms)
    report = format_report(result)
    if args.output:
        Path(args.output).write_text(report, encoding="utf-8")
        print(f"Geschrieben: {args.output}", file=sys.stderr)
    else:
        print(report)


if __name__ == "__main__":
    main()
