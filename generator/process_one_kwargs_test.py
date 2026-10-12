#!/usr/bin/env python3
"""Regression: process_one must pass profile signal kwargs into positions_to_funscript.

Until September 2026, build() inside process_one called positions_to_funscript
without peak_prominence / dynamic_range_ms / min_action_interval_ms — so
`--profile weich` set those args on the namespace but they never reached the
signal path. This test guards the wiring, not the algorithm itself.
"""

import ast
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent
src = (ROOT / "generate_funscript.py").read_text(encoding="utf-8")
tree = ast.parse(src)


def main() -> int:
    # Find the positions_to_funscript call inside process_one / build.
    hits = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        func = node.func
        name = None
        if isinstance(func, ast.Name):
            name = func.id
        elif isinstance(func, ast.Attribute):
            name = func.attr
        if name != "positions_to_funscript":
            continue
        kw = {k.arg for k in node.keywords if k.arg}
        hits.append(kw)

    if not hits:
        print("FAIL: no positions_to_funscript call found")
        return 1

    required = {
        "peak_prominence",
        "dynamic_range_ms",
        "min_interval_ms",
        "detrend_ms",
        "bandpass_hz",
    }
    # At least one call site (the process_one build path) must pass them.
    if not any(required <= kw for kw in hits):
        print("FAIL: positions_to_funscript call missing", sorted(required))
        for i, kw in enumerate(hits):
            print(f"  call[{i}] kwargs={sorted(kw)}")
        return 1

    # bandpass must be a local in process_one (parsed from args), not a bare
    # name from main() — that caused NameError in CI (Sept 2026).
    proc = None
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name == "process_one":
            proc = node
            break
    if proc is None:
        print("FAIL: process_one not found")
        return 1
    assigns = set()
    for node in ast.walk(proc):
        if isinstance(node, ast.Assign):
            for t in node.targets:
                if isinstance(t, ast.Name):
                    assigns.add(t.id)
        elif isinstance(node, ast.AnnAssign) and isinstance(node.target, ast.Name):
            assigns.add(node.target.id)
    if "bandpass" not in assigns:
        print("FAIL: process_one must assign local 'bandpass' (from args.bandpass_hz)")
        return 1

    print("OK: process_one passes peak/dynamic/min/detrend/bandpass; local bandpass set")
    return 0


if __name__ == "__main__":
    sys.exit(main())
