#!/usr/bin/env python3
"""Re-apply paint-friendly CurveItem props after taking main's GeneratorPage."""
from pathlib import Path

p = Path("app/qml/GeneratorPage.qml")
t = p.read_text()
if "Paint-friendly" in t:
    print("already patched")
    raise SystemExit(0)
old = "editMode: true\n"
new = (
    "editMode: true\n"
    "                                // Paint-friendly: crisp line, little glow, clear dots (CurveItem).\n"
    "                                lineWidth: modelData === \"stroke\" ? 1.85 : 1.4\n"
    "                                glow: 1.5\n"
    "                                fill: 0.08\n"
)
if old not in t:
    raise SystemExit("editMode: true not found")
p.write_text(t.replace(old, new, 1))
print("patched")
